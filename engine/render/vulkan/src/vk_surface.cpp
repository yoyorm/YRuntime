#include "vk_surface.h"

#include <GLFW/glfw3.h>
#include <stdexcept>

#include <yr/core/log.h>

#include "platform_window.h"
#include "vk_instance.h"

namespace yr::render::vk {

  Surface::Surface(const Instance& instance, const Window& window) {
    VkSurfaceKHR raw = nullptr;
    const VkResult result = glfwCreateWindowSurface(*instance.raiiInstance(), window.nativeHandle(), nullptr, &raw);
    if (result != VK_SUCCESS) {
      throw std::runtime_error("glfwCreateWindowSurface 失败：" + ::vk::to_string(static_cast<::vk::Result>(result)));
    }
    // raii::SurfaceKHR 接管销毁；销毁时按实例句柄调 vkDestroySurfaceKHR。
    surface_ = ::vk::raii::SurfaceKHR(instance.raiiInstance(), raw);
    YR_LOG_INFO("render.vk.surface", "surface 创建成功");
  }

} // namespace yr::render::vk