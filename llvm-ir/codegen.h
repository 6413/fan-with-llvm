#pragma once

#include <map>
#include <memory>
#include <string>

#include "ast.h"

namespace llvm {
  namespace orc {
    class KaleidoscopeJIT;
  }
}

void codegen_init();

void create_the_JIT();
std::unique_ptr<llvm::orc::KaleidoscopeJIT>& get_JIT();

void init_module();