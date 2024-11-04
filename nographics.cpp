#include <pch.h>

#include <string>
#include <condition_variable>

#include "llvm-ir/run.h"
#include "llvm-ir/library.h"

std::mutex g_mutex;
std::condition_variable g_cv;
bool ready = false;
bool processed = false;

void t0(code_t& code) {
  std::unique_lock lk(g_mutex);
  g_cv.wait(lk, [] { return ready; });

  code.init_code();
  code.recompile_code();
  code.run_code();

  processed = true;
  ready = false;
  lk.unlock();
  t0(code);
}

int main() {
  code_t code;

  auto file_name = "test.fpp";
  fan::io::file::read(file_name, &code.code_input);
  code.code_input.push_back(EOF);

  code.set_debug_cb([](const std::string& info, int flags) {

  });

  code.init_code();
  code.recompile_code();
}