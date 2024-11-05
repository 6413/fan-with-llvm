#pragma once

#include "parser.h"

#include <llvm/IR/Module.h>

struct code_t : parser_t {
  void init_code();
  void recompile_code();
  void main_loop();
  int run_code();

  void set_debug_cb(const std::function<void(const std::string&, int flags)>& cb);
  std::function<void(const std::string&, int flags)> debug_cb{ [](const std::string&, int flags) {} };
};
