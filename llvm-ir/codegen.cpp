#include "codegen.h"

#include "../include/KaleidoscopeJIT.h"
#include <llvm/ADT/STLExtras.h>
#include <llvm/Analysis/BasicAliasAnalysis.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/Interpreter.h>
#include <llvm/ExecutionEngine/GenericValue.h>

#include "parser.h"

using namespace llvm;
using namespace llvm::orc;

ExitOnError ExitOnErr;
std::map<std::string, AllocaInst*> NamedValues;
std::unique_ptr<KaleidoscopeJIT> TheJIT;

std::unique_ptr<IRBuilder<>> ir_builder;

std::unique_ptr<LLVMContext> TheContext;

static DISubroutineType* CreateFunctionType(ast_t* ast, unsigned NumArgs) {
  SmallVector<Metadata*, 8> EltTys;
  DIType* DblTy = ast->debug_info.getDoubleTy();

  // Add the result type.
  EltTys.push_back(DblTy);

  for (unsigned i = 0, e = NumArgs; i != e; ++i)
    EltTys.push_back(DblTy);

  return ast->DBuilder->createSubroutineType(ast->DBuilder->getOrCreateTypeArray(EltTys));
}


//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

void codegen_init() {

  ir_builder = std::unique_ptr<IRBuilder<>>{};

  ExitOnErr = ExitOnError{};
  NamedValues = std::map<std::string, AllocaInst*>{};
  TheJIT = std::unique_ptr<KaleidoscopeJIT>{};
}

void create_the_JIT() {
  TheJIT = ExitOnErr(KaleidoscopeJIT::Create());
}

std::unique_ptr<KaleidoscopeJIT>& get_JIT() {
  return TheJIT;
}

void init_module() {
  // Open a new module.
  TheContext = std::make_unique<LLVMContext>();

  ir_builder = std::make_unique<IRBuilder<>>(*TheContext);
}

Function* getFunction(ast_t* ast, std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto* F = ast->TheModule->getFunction(Name))
    return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FI = ast->FunctionProtos.find(Name);
  if (FI != ast->FunctionProtos.end())
    return FI->second->codegen(ast);

  // If no existing prototype exists, return null.
  return nullptr;
}

/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
static AllocaInst* CreateEntryBlockAlloca(Function* TheFunction,
  StringRef VarName) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
    TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*TheContext), nullptr, VarName);
}

Value* ast_t::NumberExprAST::codegen(ast_t* ast) {
  ast->debug_info.emit_location(this);
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value* ast_t::VariableExprAST::codegen(ast_t* ast) {
  // Look this variable up in the function.
  Value* V = NamedValues[Name];
  if (!V)
    return ast->debug_info.LogErrorV(this->loc, "Unknown variable name");

  ast->debug_info.emit_location(this);
  // Load the value.
  // PointerType::getUnqual(i8*)
  return ir_builder->CreateLoad(Type::getDoubleTy(*TheContext), V, Name.c_str());
}

Value* ast_t::UnaryExprAST::codegen(ast_t* ast) {
  Value* OperandV = Operand->codegen(ast);
  if (!OperandV)
    return nullptr;

  Function* F = getFunction(ast, std::string("unary") + Opcode);
  if (!F)
    return ast->debug_info.LogErrorV(this->loc, "Unknown unary operator");

  ast->debug_info.emit_location(this);
  return ir_builder->CreateCall(F, OperandV, "unop");
}

Value* ast_t::BinaryExprAST::codegen(ast_t* ast) {
  ast->debug_info.emit_location(this);

  // Special case '=' because we don't want to emit the LHS as an expression.
  if (Op == '=') {
    // Assignment requires the LHS to be an identifier.
    // This assume we're building without RTTI because LLVM builds that way by
    // default.  If you build LLVM with RTTI this can be changed to a
    // dynamic_cast for automatic error checking.
    VariableExprAST* LHSE = static_cast<VariableExprAST*>(LHS.get());
    if (!LHSE)
      return ast->debug_info.LogErrorV(this->loc, "destination of '=' must be a variable");
    // Codegen the RHS.
    Value* Val = RHS->codegen(ast);
    if (!Val)
      return nullptr;

    // Look up the name.
    Value* Variable = NamedValues[LHSE->getName()];
    if (!Variable)
      return ast->debug_info.LogErrorV(this->loc, "Unknown variable name");

    ir_builder->CreateStore(Val, Variable);
    return Val;
  }

  Value* L = LHS->codegen(ast);
  Value* R = RHS->codegen(ast);
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+':
    return ir_builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return ir_builder->CreateFSub(L, R, "subtmp");
  case '*':
    return ir_builder->CreateFMul(L, R, "multmp");
  case '/': return ir_builder->CreateFDiv(L, R, "divtmp");
  case '<':
    L = ir_builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return ir_builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case '>':
    L = ir_builder->CreateFCmpUGT(L, R, "cmptmp");
    return ir_builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  case '%': {
    if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
      return ir_builder->CreateSRem(L, R, "modtmp");
    }
    else if (L->getType()->isFloatingPointTy() && R->getType()->isFloatingPointTy()) {
      Function* FloorF = Intrinsic::getDeclaration(ast->TheModule.get(), Intrinsic::floor, Type::getDoubleTy(*TheContext));
      Value* Div = ir_builder->CreateFDiv(L, R, "divtmp");
      Value* FloorDiv = ir_builder->CreateCall(FloorF, { Div }, "floordivtmp");
      Value* Mult = ir_builder->CreateFMul(FloorDiv, R, "multtmp");
      return ir_builder->CreateFSub(L, Mult, "modtmp");
    }
    else {
      return ast->debug_info.LogErrorV(this->loc, "Operands to % must be both integers or both floats.");
    }
  }
  default:
    break;
  }

  // If it wasn't a builtin binary operator, it must be a user defined one. Emit
  // a call to it.
  Function* F = getFunction(ast, std::string("binary") + Op);
  assert(F && "binary operator not found!");

  Value* Ops[] = { L, R };
  return ir_builder->CreateCall(F, Ops, "binop");
}

Value* ast_t::CallExprAST::codegen(ast_t* ast) {
  ast->debug_info.emit_location(this);

  // Look up the name in the global module table.
  Function* CalleeF = getFunction(ast, Callee);
  if (!CalleeF)
    return ast->debug_info.LogErrorV(this->loc, "Unknown function referenced:" + Callee);

  // If argument mismatch error.
  if (CalleeF->arg_size() != Args.size())
    return ast->debug_info.LogErrorV(this->loc, "Incorrect # arguments passed");

  std::vector<Value*> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen(ast));
    if (!ArgsV.back())
      return nullptr;
  }

  return ir_builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Value* ast_t::IfExprAST::codegen(ast_t* ast) {
  ast->debug_info.emit_location(this);

  Value* CondV = Cond->codegen(ast);
  if (!CondV)
    return nullptr;

  // Convert condition to a bool by comparing non-equal to 0.0.
  CondV = ir_builder->CreateFCmpONE(
    CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function* TheFunction = ir_builder->GetInsertBlock()->getParent();

  // Create blocks for the then and else cases.  Insert the 'then' block at the
  // end of the function.
  BasicBlock* ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock* ElseBB = BasicBlock::Create(*TheContext, "else");
  BasicBlock* MergeBB = BasicBlock::Create(*TheContext, "ifcont");

  ir_builder->CreateCondBr(CondV, ThenBB, ElseBB);

  // Emit then value.
  ir_builder->SetInsertPoint(ThenBB);

  Value* ThenV = Then->codegen(ast);
  if (!ThenV)
    return nullptr;

  ir_builder->CreateBr(MergeBB);
  // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
  ThenBB = ir_builder->GetInsertBlock();

  // Emit else block.
  TheFunction->insert(TheFunction->end(), ElseBB);
  ir_builder->SetInsertPoint(ElseBB);

  Value* ElseV = Else->codegen(ast);
  if (!ElseV)
    return nullptr;

  ir_builder->CreateBr(MergeBB);
  // Codegen of 'Else' can change the current block, update ElseBB for the PHI.
  ElseBB = ir_builder->GetInsertBlock();

  // Emit merge block.
  TheFunction->insert(TheFunction->end(), MergeBB);
  ir_builder->SetInsertPoint(MergeBB);
  PHINode* PN = ir_builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");

  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);
  return PN;
}

// Output for-loop as:
//   var = alloca double
//   ...
//   start = startexpr
//   store start -> var
//   goto loop
// loop:
//   ...
//   bodyexpr
//   ...
// loopend:
//   step = stepexpr
//   endcond = endexpr
//
//   curvar = load var
//   nextvar = curvar + step
//   store nextvar -> var
//   br endcond, loop, endloop
// outloop:
Value* ast_t::ForExprAST::codegen(ast_t* ast) {
  Function* TheFunction = ir_builder->GetInsertBlock()->getParent();
  // Create an alloca for the variable in the entry block.
  AllocaInst* Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
  ast->debug_info.emit_location(this);

  // Emit the start code first, without 'variable' in scope.
  Value* StartVal = Start->codegen(ast);
  if (!StartVal)
    return nullptr;

  // Store the value into the alloca.
  ir_builder->CreateStore(StartVal, Alloca);

  // Within the loop, the variable is defined equal to the PHI node.
  // If it shadows an existing variable, we have to restore it, so save it now.
  AllocaInst* OldVal = NamedValues[VarName];
  NamedValues[VarName] = Alloca;

  // Create basic blocks for the loop
  BasicBlock* CondBB = BasicBlock::Create(*TheContext, "loopcond", TheFunction);
  BasicBlock* LoopBB = BasicBlock::Create(*TheContext, "loop", TheFunction);
  BasicBlock* AfterBB = BasicBlock::Create(*TheContext, "afterloop", TheFunction);

  // Fall through to condition check
  ir_builder->CreateBr(CondBB);

  // Emit condition check
  ir_builder->SetInsertPoint(CondBB);
  // Compute the end condition.
  Value* EndCond = End->codegen(ast);
  if (!EndCond)
    return nullptr;

  // Convert condition to a bool by comparing non-equal to 0.0.
  EndCond = ir_builder->CreateFCmpONE(EndCond, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
  ir_builder->CreateCondBr(EndCond, LoopBB, AfterBB);

  // Emit loop body
  ir_builder->SetInsertPoint(LoopBB);

  // Generate code for the loop body
  if (auto* C = llvm::dyn_cast<CompoundExprAST>(Body.get())) {
    for (auto& Stmt : C->getStatements()) {
      if (!Stmt->codegen(ast))
        return nullptr;
    }
  }
  else {
    if (!Body->codegen(ast))
      return nullptr;
  }

  // Emit the step value.
  Value* StepVal = nullptr;
  if (Step) {
    StepVal = Step->codegen(ast);
    if (!StepVal)
      return nullptr;
  }
  else {
    // If not specified, use 1.0.
    StepVal = ConstantFP::get(*TheContext, APFloat(1.0));
  }

  // Reload, increment, and restore the alloca.
  Value* CurVar = ir_builder->CreateLoad(Type::getDoubleTy(*TheContext), Alloca, VarName.c_str());
  Value* NextVar = ir_builder->CreateFAdd(CurVar, StepVal, "nextvar");
  ir_builder->CreateStore(NextVar, Alloca);

  // Branch back to condition check
  ir_builder->CreateBr(CondBB);

  // Insert the "after loop" block
  ir_builder->SetInsertPoint(AfterBB);

  // Restore the unshadowed variable.
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  // for expr always returns 0.0.
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Value* ast_t::VarExprAST::codegen(ast_t* ast) {
  std::vector<AllocaInst*> OldBindings;

  Function* TheFunction = ir_builder->GetInsertBlock()->getParent();

  // Register all variables and emit their initializer.
  for (unsigned i = 0, e = VarNames.size(); i != e; ++i) {
    const std::string& VarName = VarNames[i].first;
    ExprAST* Init = VarNames[i].second.get();

    // Emit the initializer before adding the variable to scope, this prevents
    // the initializer from referencing the variable itself, and permits stuff
    // like this:
    //  var a = 1 in
    //    var a = a in ...   # refers to outer 'a'.
    Value* InitVal;
    if (Init) {
      InitVal = Init->codegen(ast);
      if (!InitVal)
        return nullptr;
    }
    else { // If not specified, use 0.0.
      InitVal = ConstantFP::get(*TheContext, APFloat(0.0));
    }

    AllocaInst* Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    ir_builder->CreateStore(InitVal, Alloca);

    // Remember the old variable binding so that we can restore the binding when
    // we unrecurse.
    OldBindings.push_back(NamedValues[VarName]);

    // Remember this binding.
    NamedValues[VarName] = Alloca;
  }

  ast->debug_info.emit_location(this);

  // Codegen the body, now that all vars are in scope.
  Value* BodyVal = Body->codegen(ast);
  if (!BodyVal)
    return nullptr;

  // Pop all our variables from scope.
  for (unsigned i = 0, e = VarNames.size(); i != e; ++i)
    NamedValues[VarNames[i].first] = OldBindings[i];

  // Return the body computation.
  return BodyVal;
}

Function* ast_t::PrototypeAST::codegen(ast_t* ast) {
  // Create a vector to hold the argument types
  std::vector<Type*> ArgTypesLLVM;

  // Iterate through Args and ArgTypes to dynamically determine argument types
  for (size_t i = 0; i < Args.size(); ++i) {
    if (ArgTypes[i] == "double") {
      ArgTypesLLVM.push_back(Type::getDoubleTy(*TheContext));
    }
    else if (ArgTypes[i] == "string") {
      ArgTypesLLVM.push_back(PointerType::getUnqual(Type::getInt8Ty(*TheContext)));
    }
    else {
      ast->debug_info.LogError(this->loc, "Unknown argument type");
      return nullptr;
    }
  }

  // Create the function type
  FunctionType* FT = FunctionType::get(Type::getDoubleTy(*TheContext), ArgTypesLLVM, false);

  // Create the function
  Function* F = Function::Create(FT, Function::ExternalLinkage, Name, ast->TheModule.get());

  // Set names for all arguments
  unsigned Idx = 0;
  for (auto& Arg : F->args()) {
    Arg.setName(Args[Idx++]);
  }

  return F;
}

Function* ast_t::FunctionAST::codegen(ast_t* ast) {
  auto& P = *Proto;
  ast->FunctionProtos[Proto->getName()] = std::move(Proto);
  Function* TheFunction = getFunction(ast, P.getName());
  if (!TheFunction)
    return nullptr;

  // Create a single entry block
  BasicBlock* BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  ir_builder->SetInsertPoint(BB);

  // Debug info setup
  DIFile* Unit = ast->DBuilder->createFile(ast->debug_info.di_compile_unit->getFilename(),
    ast->debug_info.di_compile_unit->getDirectory());
  DIScope* FContext = Unit;
  unsigned LineNo = P.getLine();
  unsigned ScopeLine = LineNo;
  DISubprogram* SP = ast->DBuilder->createFunction(FContext, P.getName(), StringRef(), Unit, LineNo, CreateFunctionType(ast, TheFunction->arg_size()), ScopeLine, DINode::FlagPrototyped, DISubprogram::SPFlagDefinition);
  TheFunction->setSubprogram(SP);

  ast->debug_info.lexical_blocks.push_back(SP);
  ast->debug_info.emit_location(nullptr);

  // Record the function arguments in the NamedValues map
  NamedValues.clear();
  unsigned ArgIdx = 0;
  for (auto& Arg : TheFunction->args()) {
    AllocaInst* Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());
    DILocalVariable* D = ast->DBuilder->createParameterVariable(SP, Arg.getName(), ++ArgIdx, Unit, LineNo, ast->debug_info.getDoubleTy(), true);
    ast->DBuilder->insertDeclare(Alloca, D, ast->DBuilder->createExpression(), DILocation::get(SP->getContext(), LineNo, 0, SP), ir_builder->GetInsertBlock());
    ir_builder->CreateStore(&Arg, Alloca);
    NamedValues[std::string(Arg.getName())] = Alloca;
  }

  ast->debug_info.emit_location(Body.get());

  // Generate code for the body
  Value* RetVal = Body->codegen(ast);
  if (!RetVal) {
    TheFunction->eraseFromParent();
    if (P.isBinaryOp())
      ast->BinopPrecedence.erase(Proto->getOperatorName());
    ast->debug_info.lexical_blocks.pop_back();
    return nullptr;
  }

  // Create return instruction
  ir_builder->CreateRet(RetVal);

  ast->debug_info.lexical_blocks.pop_back();

  verifyFunction(*TheFunction);

  return TheFunction;
}