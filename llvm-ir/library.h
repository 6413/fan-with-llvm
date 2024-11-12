#pragma once

//===----------------------------------------------------------------------===//
// "Library" functions that can be "extern'd" from user code.
//===----------------------------------------------------------------------===//

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

inline std::mutex g_mutex, task_queue_mutex;
inline std::condition_variable g_cv;

inline std::vector<fan::function_t<void()>> task_queue;
inline std::vector<fan::function_t<void()>> lib_queue;

/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double x) {
  fputc((char)x, stderr);
  return 0;
}

#ifndef no_graphics
inline std::vector<loco_t::shape_t> shapes;
// cache images
inline std::unordered_map<std::string, loco_t::image_t> images;

inline std::vector<fan::graphics::model_t> models;

#endif

/// printd - printf that takes a double prints it as "%f\n", returning 0.
extern "C" DLLEXPORT double printd(double x) {
#ifndef no_graphics
  std::lock_guard<std::mutex> lock(task_queue_mutex);
  task_queue.push_back([=] {
    fan::printcl((uint64_t)x);
  });
#endif
  return 0;
}
extern "C" DLLEXPORT double printcl(const char* x) {
#ifndef no_graphics
  std::lock_guard<std::mutex> lock(task_queue_mutex);
  task_queue.push_back([=] {
    fan::printcl(x);
  });
#endif
  return 0;
}


extern "C" DLLEXPORT double string_test(const char* str) {
#ifndef no_graphics
  std::lock_guard<std::mutex> lock(task_queue_mutex);
  task_queue.push_back([=] {
    fan::print(str);
  });
#endif
  return 0;
}

static int depth = 0;

extern "C" DLLEXPORT double rectangle1(double px, double py, double sx, double sy, double color, double angle) {
#ifndef no_graphics
  std::lock_guard<std::mutex> lock(task_queue_mutex);
  task_queue.push_back([=] {
    shapes.push_back(fan::graphics::rectangle_t{ {
        .position = fan::vec3(px, py, depth++),
        .size = fan::vec2(sx, sx),
        .color = fan::color::hex((uint32_t)color),
        .angle = angle
    } });
  });
#endif
  return 0;
}

extern "C" DLLEXPORT double rectangle0(double px, double py, double sx, double sy) {
#ifndef no_graphics
  std::lock_guard<std::mutex> lock(task_queue_mutex);
  task_queue.push_back([=] {
    shapes.push_back(fan::graphics::rectangle_t{ {
       .position = fan::vec3(px, py, depth++),
       .size = fan::vec2(sx, sx),
       .color = fan::color::hex((uint32_t)fan::random::color().get_hex())
   } });
  });
#endif
  return 0;
}

extern "C" DLLEXPORT double sprite2(const char* cpath, double px, double py, double sx, double sy, double anglex, double angley, double anglez) {
#ifndef no_graphics
  std::lock_guard<std::mutex> lock(task_queue_mutex);
  task_queue.push_back([=, str = std::string(cpath)] {
    auto found = images.find(str.c_str());
    loco_t::image_t image;
    if (found != images.end()) {
      image = found->second;
    }
    else {
      image = gloco->image_load(str.c_str());
      images[str.c_str()] = image;
    }
    shapes.push_back(fan::graphics::sprite_t{ {
        .position = fan::vec3(px, py, depth++),
        .size = fan::vec2(sx, sx),
        .angle = fan::vec3(anglex, angley, anglez),
        .image = image
    } });
    //return shapes.back().NRI;
  });
#endif
  return 0;
}

extern "C" DLLEXPORT double set_position(double shape, double px, double py) {
#ifndef no_graphics
  std::lock_guard<std::mutex> lock(task_queue_mutex);
  task_queue.push_back([=] {
    decltype(loco_t::shape_t::NRI) nri = shape;
    (reinterpret_cast<loco_t::shape_t*>(&nri))->set_position(fan::vec2(px, py));
  });
#endif
  return 0;
}
extern "C" DLLEXPORT double sprite1(const char* cpath, double px, double py, double sx, double sy, double angle) {
#ifndef no_graphics
  std::lock_guard<std::mutex> lock(task_queue_mutex);
  task_queue.push_back([=, str = std::string(cpath)] {
    auto found = images.find(str.c_str());
    loco_t::image_t image;
    if (found != images.end()) {
      image = found->second;
    }
    else {
      image = gloco->image_load(str.c_str());
      images[str.c_str()] = image;
    }
    shapes.push_back(fan::graphics::sprite_t{ {
        .position = fan::vec3(px, py, depth++),
        .size = fan::vec2(sx, sx),
        .angle = fan::vec3(0, 0, angle),
        .image = image
    } });
  });
#endif
  return 0;
}

extern "C" DLLEXPORT double sprite0(const char* cpath, double px, double py, double sx, double sy) {
#ifndef no_graphics
  std::lock_guard<std::mutex> lock(task_queue_mutex);
  task_queue.push_back([=, str = std::string(cpath)] {
    auto found = images.find(str.c_str());
    loco_t::image_t image;
    if (found != images.end()) {
      image = found->second;
    }
    else {
      image = gloco->image_load(str.c_str());
      images[str.c_str()] = image;
    }
    shapes.push_back(fan::graphics::sprite_t{ {
        .position = fan::vec3(px, py, depth++),
        .size = fan::vec2(sx, sx),
        .image = image
    } });
  });
#endif
  return 0;
}

extern "C" DLLEXPORT double model3d(const char* cpath, double px, double py, double pz, double scale) {
#ifndef no_graphics
  std::lock_guard<std::mutex> lock(task_queue_mutex);
  task_queue.push_back([=] {
    fan::graphics::model_t::properties_t mp;
    mp.path = cpath;
    mp.model = mp.model.translate(fan::vec3(px, py, pz)).scale(scale);
    models.push_back(mp);
    gloco->m_pre_draw.push_back([model_id = models.size() - 1] {
      models[model_id].draw();
    });
  });
#endif
  return 0;
}

inline bool code_sleep = false;

extern "C" DLLEXPORT double clear() {
#ifndef no_graphics
  std::lock_guard<std::mutex> lock(task_queue_mutex);
  task_queue.push_back([=] {
    shapes.clear();
  });
  depth = 0;
#endif
  return 0;
}

extern "C" DLLEXPORT double sleep_s(double x) {
#ifndef no_graphics
  std::this_thread::sleep_for(std::chrono::duration<double>(x));
#endif
  return 0;
}

void clean_up() {
  code_sleep = false;
  lib_queue.clear();
}