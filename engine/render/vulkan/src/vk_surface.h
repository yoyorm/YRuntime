#pragma once

#include "vk_hpp.h"

namespace yr::render::vk {

  class Instance;
  class Window;

  // VkSurfaceKHR 的所有者；依赖 instance + window，必须在二者之前销毁。
  class Surface {
  public:
    Surface(const Instance& instance, const Window& window);
    ~Surface() = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    Surface(Surface&&) = delete;
    Surface& operator=(Surface&&) = delete;

    [[nodiscard]] ::vk::SurfaceKHR handle() const noexcept { return *surface_; }

  private:
    ::vk::raii::SurfaceKHR surface_{nullptr};
  };

} // namespace yr::render::vk