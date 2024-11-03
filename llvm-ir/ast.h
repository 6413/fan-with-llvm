#pragma once

#include "lexer.h"

extern std::unique_ptr<llvm::LLVMContext> TheContext;


//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//
struct ast_t : lexer_t {

  static llvm::raw_ostream& indent(llvm::raw_ostream& O, int size) {
    return O << std::string(size, ' ');
  }
  /// ExprAST - Base class for all expression nodes.
  class ExprAST {
  public:
    enum ExprKind {
      Expr_Binary,
      Expr_Call,
      Expr_For,
      Expr_If,
      Expr_Number,
      Expr_Unary,
      Expr_Variable,
      Expr_Var,
      Expr_Compound,
      Expr_String
    };
  private:
    source_location_t loc;
    ExprKind Kind;

  public:
    // lazy implement xd
    //ExprAST(source_location_t loc = CurLoc) : loc(loc), Kind(Expr_Binary){}
    ExprAST(ExprKind K, source_location_t loc) : loc(loc), Kind(K) {}

    virtual ~ExprAST() {}
    virtual llvm::Value* codegen(ast_t* ast) = 0;
    int getLine() const { return loc.line; }
    ExprKind getKind() const { return Kind; }
    int getCol() const { return loc.col; }
    virtual llvm::raw_ostream& dump(llvm::raw_ostream& out, int ind) {
      return out << ':' << getLine() << ':' << getCol() << '\n';
    }
  };

  /// NumberExprAST - Expression class for numeric literals like "1.0".
  class NumberExprAST : public ExprAST {
    double Val;

  public:
    NumberExprAST(source_location_t loc, double Val) : ExprAST(Expr_Number, loc), Val(Val) {}
    llvm::raw_ostream& dump(llvm::raw_ostream& out, int ind) override {
      return ExprAST::dump(out << Val, ind);
    }
    llvm::Value* codegen(ast_t* ast) override;
  };

  /// VariableExprAST - Expression class for referencing a variable, like "a".
  class VariableExprAST : public ExprAST {
    std::string Name;

  public:
    VariableExprAST(source_location_t loc, const std::string& Name)
      : ExprAST(Expr_Variable, loc), Name(Name) {}
    const std::string& getName() const { return Name; }
    llvm::Value* codegen(ast_t* ast) override;
    llvm::raw_ostream& dump(llvm::raw_ostream& out, int ind) override {
      return ExprAST::dump(out << Name, ind);
    }
  };

  /// UnaryExprAST - Expression class for a unary operator.
  class UnaryExprAST : public ExprAST {
    char Opcode;
    std::unique_ptr<ExprAST> Operand;

  public:
    UnaryExprAST(source_location_t loc, char Opcode, std::unique_ptr<ExprAST> Operand)
      : ExprAST(Expr_Unary, loc), Opcode(Opcode), Operand(std::move(Operand)) {}
    llvm::Value* codegen(ast_t* ast) override;
    llvm::raw_ostream& dump(llvm::raw_ostream& out, int ind) override {
      ExprAST::dump(out << "unary" << Opcode, ind);
      Operand->dump(out, ind + 1);
      return out;
    }
  };

  /// BinaryExprAST - Expression class for a binary operator.
  class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

  public:
    BinaryExprAST(source_location_t loc, char Op, std::unique_ptr<ExprAST> LHS,
      std::unique_ptr<ExprAST> RHS)
      : ExprAST(Expr_Binary, loc), Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
    llvm::Value* codegen(ast_t* ast) override;
    llvm::raw_ostream& dump(llvm::raw_ostream& out, int ind) override {
      ExprAST::dump(out << "binary" << Op, ind);
      LHS->dump(indent(out, ind) << "LHS:", ind + 1);
      RHS->dump(indent(out, ind) << "RHS:", ind + 1);
      return out;
    }
  };

  /// CallExprAST - Expression class for function calls.
  class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

  public:
    CallExprAST(source_location_t loc, const std::string& Callee,
      std::vector<std::unique_ptr<ExprAST>> Args)
      : ExprAST(Expr_Call, loc), Callee(Callee), Args(std::move(Args)) {}
    llvm::Value* codegen(ast_t* ast) override;
    llvm::raw_ostream& dump(llvm::raw_ostream& out, int ind) override {
      ExprAST::dump(out << "call " << Callee, ind);
      for (const auto& Arg : Args)
        Arg->dump(indent(out, ind + 1), ind + 1);
      return out;
    }
  };

  /// IfExprAST - Expression class for if/then/else.
  class IfExprAST : public ExprAST {
    std::unique_ptr<ExprAST> Cond, Then, Else;

  public:
    IfExprAST(source_location_t loc, std::unique_ptr<ExprAST> Cond,
      std::unique_ptr<ExprAST> Then, std::unique_ptr<ExprAST> Else)
      : ExprAST(Expr_If, loc), Cond(std::move(Cond)), Then(std::move(Then)),
      Else(std::move(Else)) {}
    llvm::Value* codegen(ast_t* ast) override;
    llvm::raw_ostream& dump(llvm::raw_ostream& out, int ind) override {
      ExprAST::dump(out << "if", ind);
      Cond->dump(indent(out, ind) << "Cond:", ind + 1);
      Then->dump(indent(out, ind) << "Then:", ind + 1);
      Else->dump(indent(out, ind) << "Else:", ind + 1);
      return out;
    }
  };

  class CompoundExprAST : public ExprAST {
    std::vector<std::unique_ptr<ExprAST>> Expressions;

  public:
    CompoundExprAST(source_location_t loc, std::vector<std::unique_ptr<ExprAST>> Expressions)
      : ExprAST(Expr_Compound, loc), Expressions(std::move(Expressions)) {}

    std::vector<std::unique_ptr<ExprAST>>& getStatements() { return Expressions; }

    llvm::Value* codegen(ast_t* ast) override {
      for (auto& Expr : Expressions) {
        if (!Expr->codegen(ast))
          return nullptr;
      }

      return llvm::Constant::getNullValue(llvm::Type::getDoubleTy(*TheContext));
    }
    static bool classof(const ExprAST* E) {
      auto x = E->getKind();
      return E->getKind() == Expr_Compound;
    }
  };


  /// ForExprAST - Expression class for for/in.
  class ForExprAST : public ExprAST {
    std::string VarName;
    std::unique_ptr<ExprAST> Start, End, Step, Body;

  public:
    ForExprAST(source_location_t loc, const std::string& VarName, std::unique_ptr<ExprAST> Start,
      std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
      std::unique_ptr<ExprAST> Body)
      : ExprAST(Expr_For, loc), VarName(VarName), Start(std::move(Start)), End(std::move(End)),
      Step(std::move(Step)), Body(std::move(Body)) {}
    llvm::Value* codegen(ast_t* ast) override;
    llvm::raw_ostream& dump(llvm::raw_ostream& out, int ind) override {
      ExprAST::dump(out << "for", ind);
      Start->dump(indent(out, ind) << "Cond:", ind + 1);
      End->dump(indent(out, ind) << "End:", ind + 1);
      Step->dump(indent(out, ind) << "Step:", ind + 1);
      Body->dump(indent(out, ind) << "Body:", ind + 1);
      return out;
    }
  };

  /// VarExprAST - Expression class for var/in
  class VarExprAST : public ExprAST {
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
    std::unique_ptr<ExprAST> Body;

  public:
    VarExprAST(
      source_location_t loc,
      std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames,
      std::unique_ptr<ExprAST> Body)
      : ExprAST(Expr_Var, loc), VarNames(std::move(VarNames)), Body(std::move(Body)) {}
    llvm::Value* codegen(ast_t* ast) override;
    llvm::raw_ostream& dump(llvm::raw_ostream& out, int ind) override {
      ExprAST::dump(out << "var", ind);
      for (const auto& NamedVar : VarNames)
        NamedVar.second->dump(indent(out, ind) << NamedVar.first << ':', ind + 1);
      Body->dump(indent(out, ind) << "Body:", ind + 1);
      return out;
    }
  };

  /// PrototypeAST - This class represents the "prototype" for a function,
  /// which captures its name, and its argument names (thus implicitly the number
  /// of arguments the function takes), as well as if it is an operator.
  class PrototypeAST {
    std::string Name;
    std::vector<std::string> Args;
    std::vector<std::string> ArgTypes; // New vector for argument types
    bool IsOperator;
    unsigned Precedence;
    int Line;

  public:
    PrototypeAST(source_location_t loc, const std::string& Name,
      std::vector<std::string> Args, std::vector<std::string> ArgTypes,
      bool IsOperator = false, unsigned Prec = 0)
      : Name(Name), Args(std::move(Args)), ArgTypes(std::move(ArgTypes)),
      IsOperator(IsOperator), Precedence(Prec), Line(loc.line) {}

    llvm::Function* codegen(ast_t* ast);
    const std::string& getName() const { return Name; }

    bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
    bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

    char getOperatorName() const {
      assert(isUnaryOp() || isBinaryOp());
      return Name[Name.size() - 1];
    }

    unsigned getBinaryPrecedence() const { return Precedence; }
    int getLine() const { return Line; }
  };


  /// FunctionAST - This class represents a function definition itself.
  class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

  public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto,
      std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
    llvm::Function* codegen(ast_t* ast);
    llvm::raw_ostream& dump(llvm::raw_ostream& out, int ind) {
      indent(out, ind) << "FunctionAST\n";
      ++ind;
      indent(out, ind) << "Body:";
      return Body ? Body->dump(out, ind) : out << "null\n";
    }
    const PrototypeAST& getProto() const;
    const std::string& getName() const;
  };

  class StringExprAST : public ExprAST {
    std::string Val;

  public:
    StringExprAST(source_location_t loc, const std::string& Val) : ExprAST(Expr_String, loc), Val(Val) {}
    llvm::Value* codegen(ast_t* ast) override {
      return ir_builder->CreateGlobalStringPtr(Val, "str");
    }
  };

  /// LogError* - These are little helper functions for error handling.
  std::unique_ptr<ExprAST> LogError(const char* Str) {
    fprintf(stderr, "Error: %s %d %d\n", Str, cursor_location.line, cursor_location.col);
    return nullptr;
  }

  llvm::Value* LogErrorV(const char* Str) {
    LogError(Str);
    return nullptr;
  }

  std::unique_ptr<PrototypeAST> LogErrorP(const char* Str) {
    LogError(Str);
    return nullptr;
  }

  /// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
  std::map<char, int> BinopPrecedence{
    {'=', 2},
    {'<', 10},
    {'>', 10},
    {'+', 20},
    {'-', 20},
    {'*', 40},
    {'/', 40},
    { '%', 40 },
  };
  std::map<std::string, std::unique_ptr<ast_t::PrototypeAST>> FunctionProtos;
};