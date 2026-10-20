#include "platform_window.h"

#include <stdexcept>

#include <yr/core/log.h>

namespace yr::render::vk {

  namespace {

    // glfwInit/glfwTerminate 是进程级资源：只初始化一次，析构不重复 terminate。
    bool glfwInitialized = false;

    [[nodiscard]] bool ensureGlfwInitialized() {
      if (glfwInitialized) {
        return true;
      }
      if (glfwInit() != GLFW_TRUE) {
        YR_LOG_ERROR("render.vk.window", "glfwInit 失败");
        return false;
      }
      glfwInitialized = true;
      return true;
    }

  } // namespace

  Window::Window() {
    if (!ensureGlfwInitialized()) {
      throw std::runtime_error("glfwInit failed");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(1280, 720, "YRuntime", nullptr, nullptr);
    if (window_ == nullptr) {
      YR_LOG_ERROR("render.vk.window", "glfwCreateWindow 失败");
      throw std::runtime_error("glfwCreateWindow failed");
    }
  }

  Window::~Window() {
    destroy();
  }

  void Window::pollEvents() const {
    glfwPollEvents();
  }

  bool Window::shouldClose() const {
    if (window_ == nullptr) {
      return true;
    }
    return glfwWindowShouldClose(window_) != GLFW_FALSE;
  }

  int Window::width() const {
    if (window_ == nullptr) {
      return 0;
    }
    int widthPixels = 0;
    int heightPixels = 0;
    glfwGetFramebufferSize(window_, &widthPixels, &heightPixels);
    return widthPixels;
  }

  int Window::height() const {
    if (window_ == nullptr) {
      return 0;
    }
    int widthPixels = 0;
    int heightPixels = 0;
    glfwGetFramebufferSize(window_, &widthPixels, &heightPixels);
    return heightPixels;
  }

  void Window::destroy() {
    if (window_ == nullptr) {
      return;
    }
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }

} // namespace yr::render::vk