#pragma once

#include <GLFW/glfw3.h>

namespace yr::render::vk {

  // GLFW 窗口 + 进程级 glfwInit 的 RAII 所有者
  class Window {
  public:
    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    void pollEvents() const;
    // 已 destroy()（无窗口）时恒为 true。
    [[nodiscard]] bool shouldClose() const;
    // 帧缓冲像素尺寸；最小化时为 0，调用方需自行处理。
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] GLFWwindow* nativeHandle() const noexcept { return window_; }

    void destroy();

  private:
    GLFWwindow* window_ = nullptr;
  };

} // namespace yr::render::vk