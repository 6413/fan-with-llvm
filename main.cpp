#include <pch.h>

#include <string>
#include <condition_variable>

#include "llvm-ir/run.h"
#include "llvm-ir/library.h"

struct pile_t {
  loco_t loco;
}pile;

void init_graphics(TextEditor& editor, const char* file_name) {
  static int current_font = 2;

  static bool block_zoom[2]{};

  static float font_scale_factor = 1.0f;
  pile.loco.window.add_buttons_callback([&](const auto& d) {
    if (d.state != fan::mouse_state::press) {
      return;
    }
    if (pile.loco.window.key_pressed(fan::key_left_control) == false) {
      return;
    }

    auto& io = ImGui::GetIO();
    switch (d.button) {
    case fan::mouse_scroll_up: {
      if (block_zoom[0] == true) {
        break;
      }
      font_scale_factor *= 1.1;
      block_zoom[1] = false;
      break;
    }
    case fan::mouse_scroll_down: {
      if (block_zoom[1] == true) {
        break;
      }
      font_scale_factor *= 0.9;
      block_zoom[0] = false;
      break;
    }
    }

    if (font_scale_factor > 1.5) {
      current_font++;
      if (current_font > std::size(pile.loco.fonts) - 1) {
        current_font = std::size(pile.loco.fonts) - 1;
        block_zoom[0] = true;
      }
      else {
        io.FontDefault = pile.loco.fonts[current_font];
        font_scale_factor = 1;
      }
    }

    if (font_scale_factor < 0.5) {
      current_font--;
      if (current_font < 0) {
        current_font = 0;
        block_zoom[1] = true;
      }
      else {
        io.FontDefault = pile.loco.fonts[current_font];
        font_scale_factor = 1;
      }
    }

    io.FontGlobalScale = font_scale_factor;
    return;
  });

  editor.ignore_comments = false;
  editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
  editor.SetPalette(editor.GetRetroBluePalette());
  editor.SetTabSize(2);
  editor.SetShowWhitespaces(false);

  fan::string str;
  fan::io::file::read(
    file_name,
    &str
  );

  editor.SetText(str);
}

bool ready = false, processed = false;

void t0(code_t& code) {
  std::unique_lock lk(g_mutex);
  g_cv.wait(lk, [] { return ready; });

  code.init_code();
  fan::time::clock c;
  c.start();

  code.recompile_code();
  uint64_t compile_time = c.elapsed();
  code.run_code();

  lib_queue.push_back([elapsed = c.elapsed(), compile_time] {
    fan::printclh(loco_t::console_t::highlight_e::success, "Compile time: ", compile_time / 1e6, "ms");
    fan::printclh(loco_t::console_t::highlight_e::success, "Program boot time: ", elapsed / 1e6, "ms");
  });

  processed = true;
  ready = false;
  lk.unlock();
  t0(code);
}

struct debug_info_t {
  std::string info;
  int flags;
};

int main() {
  code_t code;

  std::vector<debug_info_t> debug_info;
  code.set_debug_cb([&debug_info](const std::string& info, int flags) {
    lib_queue.push_back([=, &debug_info] {
      if (flags != 1) { // err 
        debug_info.push_back({ .info = info, .flags = flags });
      }
      else {
        fan::printclh(loco_t::console_t::highlight_e::error, info);
      }
    });
  });

  std::jthread t(t0, std::ref(code));
  t.detach();

  pile.loco.render_console = true;

  pile.loco.console.commands.add("clear_shapes", [](const fan::commands_t::arg_t& args) {
    shapes.clear();
  }).description = "clears all shapes within the program";

  pile.loco.console.commands.add("print_debug", [&debug_info](const fan::commands_t::arg_t& args) {
    for (const auto& i : debug_info) {
      fan::printclh(i.flags, i.info);
    }
  }).description = "prints compile debug information";

  TextEditor editor, input;
  auto file_name = "test.fpp";

  init_graphics(editor, file_name);

  code.tab_size = editor.GetTabSize();

  pile.loco.input_action.add_keycombo({ fan::key_left_control, fan::key_s }, "save_file");
  pile.loco.input_action.add_keycombo({ fan::key_f5 }, "compile_and_run");

  uint32_t task_id = 0, sleep_id = 0;

  auto compile_and_run = [&editor, &code, &task_id, &sleep_id] {

    models.clear();
    gloco->m_pre_draw.clear();
    fan::printclh(loco_t::console_t::highlight_e::info, "Compiling...");
    code.code_input = editor.GetText();
    if (code.code_input.back() == '\n') {
      code.code_input.pop_back();
    }
    code.code_input.push_back(EOF);

    {
      std::lock_guard lk(g_mutex);
      ready = true;
    }
    g_cv.notify_one();
  };

  auto& camera = gloco->camera_get(gloco->perspective_camera.camera);

  bool window_focused = false;
  //
  fan::vec2 motion = 0;
  pile.loco.window.add_mouse_motion([&](const auto& d) {
    if (window_focused == false) {
      return;
    }
    motion = d.motion;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
      camera.rotate_camera(d.motion);
    }
  });

  pile.loco.loop([&] {

    {
      std::lock_guard<std::mutex> lock(task_queue_mutex);
      for (auto& i : task_queue) {
        i();
      }
      task_queue.clear();
    }

    ImGui::Begin("window");
    ImGui::SameLine();

    if (
      (ImGui::Button("compile & run") ||
      pile.loco.input_action.is_active("compile_and_run"))
      ) {
      compile_and_run();
    }
    editor.Render("editor");
    ImGui::End();
    ImGui::Begin("Content");
    if (window_focused = ImGui::IsWindowFocused()) {
      camera.move(100);
    }
    pile.loco.set_imgui_viewport(pile.loco.orthographic_camera.viewport);
    fan::vec2 viewport_size = ImGui::GetContentRegionAvail();
    pile.loco.camera_set_ortho(
      pile.loco.orthographic_camera.camera,
      fan::vec2(0, viewport_size.x),
      fan::vec2(0, viewport_size.y)
    );

    ImGui::End();
    if (pile.loco.input_action.is_active("save_file")) {
      std::string str = editor.GetText();
      fan::io::file::write(file_name, str.substr(0, std::max(size_t(0), str.size() - 1)), std::ios_base::binary);
    }

    if (processed) {
      for (const auto& i : lib_queue) {
        i();
      }
      lib_queue.clear();

      //std::lock_guard<std::mutex> lk(task_queue_mutex);
      //if (code_sleep) {
      //  if (sleep_timers[sleep_id].finished()) {
      //    ++sleep_id;
      //    code_sleep = false;
      //  }
      //}
      //for (uint32_t i = task_id; code_sleep == false && i < task_queue.size(); ++i) {
      //  task_queue[task_id++]();
      //  if (code_sleep) {
      //    sleep_timers[sleep_id].start();
      //    break;
      //  }
      //}
      //if (task_id == task_queue.size()) {
      //  task_id = 0;
      //  processed = false;
      //  sleep_id = 0;
      //  clean_up();
      //}
    }

  });
}