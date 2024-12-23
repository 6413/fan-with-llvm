#include "run.h"

#include "../include/KaleidoscopeJIT.h"
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/IR/LegacyPassManager.h>

#include "parser.h"
#include "codegen.h"

using namespace llvm;

void code_t::init_code() {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  debug_info.init();

  TheContext = std::unique_ptr<LLVMContext>{};
  TheModule = std::unique_ptr<Module>{};
  FunctionProtos = std::map<std::string, std::unique_ptr<ast_t::PrototypeAST>>{};

  codegen_init();

  identifier_string = std::string(); // Filled in if tok_identifier
  double_value = double();             // Filled in if tok_number

  CurTok = 0;
  last_char = ' ';

  cursor_location = source_location_t();
  lex_location = source_location_t{ 1, 0 };

  // Prime the first token.
  getNextToken();

  create_the_JIT();

  init_module();

  TheModule = std::make_unique<Module>("my cool jit", *TheContext);
  TheModule->setDataLayout(get_JIT()->getDataLayout());

  // Add the current debug info version into the module.
  TheModule->addModuleFlag(Module::Warning, "Debug Info Version", DEBUG_METADATA_VERSION);

  // Darwin only supports dwarf2.
  if (Triple(llvm::sys::getProcessTriple()).isOSDarwin())
    TheModule->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 2);

  // Construct the DIBuilder, we do this here because we need the module.
  DBuilder = std::make_unique<DIBuilder>(*TheModule);

  // Create the compile unit for the module.
  // Currently down as "fib.ks" as a filename since we're redirecting stdin
  // but we'd like actual source locations.
  debug_info.di_compile_unit = DBuilder->createCompileUnit(
    dwarf::DW_LANG_C, DBuilder->createFile("fib.ks", "."), "Kaleidoscope Compiler", false, "", 0);

  {
    {
      std::vector<std::string> Args{ "x" }; // parameter names
      std::vector<Type*> params(Args.size(), Type::getDoubleTy(*TheContext));
      FunctionType* FT =
        FunctionType::get(Type::getDoubleTy(*TheContext), params, false);

      Function* F =
        Function::Create(FT, Function::ExternalLinkage, "printd", TheModule.get());

      // Set names for all arguments.
      unsigned Idx = 0;
      for (auto& Arg : F->args())
        Arg.setName(Args[Idx++]);
    }
    {
      std::vector<std::string> Args{ "x" }; // parameter names
      std::vector<Type*> params(Args.size(), PointerType::getUnqual(Type::getInt8Ty(*TheContext)));
      FunctionType* FT =
        FunctionType::get(Type::getDoubleTy(*TheContext), params, false);

      Function* F =
        Function::Create(FT, Function::ExternalLinkage, "printcl", TheModule.get());

      // Set names for all arguments.
      unsigned Idx = 0;
      for (auto& Arg : F->args())
        Arg.setName(Args[Idx++]);
    }
    {
      //CreateUnaryPrototype('!', this);
     // CreateUnaryPrototype('|', this);
    }
  }
}

/// top ::= definition | external | expression | ';'
void code_t::main_loop() {
  while (true) {
    switch (CurTok) {
    case tok_eof:
      code_input.clear(); // clear buffer
      return;
    case ';': // ignore top-level semicolons.
      getNextToken();
      break;
    case tok_definition:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    case 0: {
      return;
    }
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}


int code_t::run_code() {

  if (debug_info.compiled == false) {
    // flag 1 corresponds to error - uses fan console highlight enum
    debug_cb(debug_info.error_log, 1);
    debug_cb("Failed to compile", 1);
    TheModule.reset();// idk why this needs to be here, otherwise corruption, maybe destruction order?
    return 1;
  }

  // Create the JIT engine and move the module into it
  std::string ErrorStr;
  std::unique_ptr<ExecutionEngine> EE(
    EngineBuilder(std::move(TheModule))
    .setErrorStr(&ErrorStr)
    .setOptLevel(CodeGenOptLevel::Default)
    .create()
  );

  if (!EE) {
    errs() << "Failed to create ExecutionEngine: " << ErrorStr << "\n";
    return 1;
  }

  //{
  //  using namespace object;
  //  std::string objectFileName("a.o");

  //  ErrorOr<std::unique_ptr<MemoryBuffer>> buffer =
  //    MemoryBuffer::getFile(objectFileName.c_str());

  //  if (!buffer) {
  //    fan::throw_error("failed to open file");
  //  }

  //  Expected<std::unique_ptr<ObjectFile>> objectOrError =
  //    ObjectFile::createObjectFile(buffer.get()->getMemBufferRef());

  //  if (!objectOrError) {
  //    fan::throw_error("failed to open file");
  //  }

  //  std::unique_ptr<ObjectFile> objectFile(std::move(objectOrError.get()));

  //  auto owningObject = OwningBinary<ObjectFile>(std::move(objectFile),
  //    std::move(buffer.get()));

  //  EE->addObjectFile(std::move(owningObject));
  //}

  EE->finalizeObject();

  // Assuming you have a function named 'main' to execute
  Function* MainFn = EE->FindFunctionNamed("main");
  if (!MainFn) {
    errs() << "'main' function not found in module.\n";
    return 1;
  }

  std::vector<GenericValue> NoArgs;
  GenericValue GV = EE->runFunction(MainFn, NoArgs);
  //  std::stringstream oss;
    // oss << "Result: " << std::fixed << std::setprecision(0) << GV.DoubleVal << std::endl;
    // Print the result
    //fan::printcl("result: ", GV.DoubleVal);
    //std::cout << "Result: " << std::fixed << std::setprecision(0) << GV.DoubleVal << std::endl;
  return 0;
}

void code_t::recompile_code() {
  auto start = std::chrono::steady_clock::now();

  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  //// Run the main "interpreter loop" now.
  main_loop();

  //parse_input();

  // Finalize the debug info.
  DBuilder->finalize();

  if (debug_info.compiled == false) {
    return;
  }

  // ir output
  std::string str;
  llvm::raw_string_ostream rso(str);
  TheModule->print(rso, nullptr);
  rso.flush();
  debug_cb(str, 0);

  auto TargetTriple = sys::getDefaultTargetTriple();
  TheModule->setTargetTriple(TargetTriple);
  std::string Error;
  auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);

  // Print an error and exit if we couldn't find the requested target.
  // This generally occurs if we've forgotten to initialize the
  // TargetRegistry or we have a bogus target triple.
  if (!Target) {
    errs() << Error;
    return;
  }

  auto CPU = "generic";
  auto Features = "";
  TargetOptions opt;
  auto TheTargetMachine = Target->createTargetMachine(TargetTriple, CPU, Features, opt, Reloc::PIC_);
  TheModule->setDataLayout(TheTargetMachine->createDataLayout());
  auto Filename = "output.o";
  std::error_code EC;
  raw_fd_ostream dest(Filename, EC, sys::fs::OF_None);
  if (EC) {
    errs() << "Could not open file: " << EC.message();
    return;
  }

  legacy::PassManager pass;
  auto FileType = CodeGenFileType::ObjectFile;
  if (TheTargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    errs() << "TheTargetMachine can't emit a file of this type";
    return;
  }

  pass.run(*TheModule);
  dest.flush();
  outs() << "Wrote " << Filename << "\n";
}

void code_t::set_debug_cb(const std::function<void(const std::string&, int flags)>& cb) {
  debug_cb = cb;
}