#pragma once

//===----------------------------------------------------------------------===//
// "Library" functions that can be "extern'd" from user code.
//===----------------------------------------------------------------------===//

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

std::mutex task_queue_mutex;
inline std::vector<fan::function_t<void()>> task_queue;
inline std::vector<fan::time::clock> sleep_timers;

static void add_task(auto l) {
  task_queue_mutex.lock();
  task_queue.push_back(l);
  task_queue_mutex.unlock();
}

/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double x) {
  fputc((char)x, stderr);
  return 0;
}

inline std::vector<loco_t::shape_t> shapes;
// cache images
inline std::unordered_map<std::string, loco_t::image_t> images;

/// printd - printf that takes a double prints it as "%f\n", returning 0.
extern "C" DLLEXPORT double printd(double x) {
  add_task([=] {
    fan::printcl((uint64_t)x);
  });
  return 0;
}
extern "C" DLLEXPORT double printcl(const char* x) {
  add_task([str = std::string(x)] {
    fan::printcl(str);
    });
  return 0;
}


extern "C" DLLEXPORT double string_test(const char* str) {
  fan::print(str);
  return 0;
}

static int depth = 0;

extern "C" DLLEXPORT double rectangle1(double px, double py, double sx, double sy, double color, double angle) {
  add_task([=] {
    shapes.push_back(fan::graphics::rectangle_t{ {
        .position = fan::vec3(px, py, depth++),
        .size = fan::vec2(sx, sx),
        .color = fan::color::hex((uint32_t)color),
        .angle = angle
    } });
  });
  return 0;
}

extern "C" DLLEXPORT double rectangle0(double px, double py, double sx, double sy) {
  return rectangle1(px, py, sx, sy, fan::random::color().get_hex(), 0);
}

extern "C" DLLEXPORT double sprite2(const char* cpath, double px, double py, double sx, double sy, double anglex, double angley, double anglez) {
  add_task([=, path = std::string(cpath)] {
    auto found = images.find(path);
    loco_t::image_t image;
    if (found != images.end()) {
      image = found->second;
    }
    else {
      image = gloco->image_load(path);
      images[path] = image;
    }
    shapes.push_back(fan::graphics::sprite_t{ {
        .position = fan::vec3(px, py, depth++),
        .size = fan::vec2(sx, sx),
        .angle = fan::vec3(anglex, angley, anglez),
        .image = image
    } });
    });
  return 0;
}

extern "C" DLLEXPORT double sprite1(const char* cpath, double px, double py, double sx, double sy, double angle) {
  return sprite2(cpath, px, py, sx, sy, 0, 0, angle);
}

extern "C" DLLEXPORT double sprite0(const char* cpath, double px, double py, double sx, double sy) {
  return sprite1(cpath, px, py, sx, sy, 0);
}

inline bool code_sleep = false;

extern "C" DLLEXPORT double clear() {
  add_task([&] {
    shapes.clear();
    depth = 0;
  });
  return 0;
}

extern "C" DLLEXPORT double sleep_s(double x) {
  add_task([=] {
    code_sleep = true;
    sleep_timers.push_back(fan::time::seconds(x));
  });
  return 0;
}

void clean_up() {
  task_queue.clear();
  sleep_timers.clear();
  code_sleep = false;
}