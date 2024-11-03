#pragma once

#include "parser.h"

namespace llvm {
  struct Module;
}

struct code_t : parser_t {
  void init_code();
  void recompile_code();
  void main_loop();
  int run_code();

  void printDebugInfo(llvm::Module& M);
};
