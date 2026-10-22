#pragma once

#include <cstdint>
#include <limits>
#include <span>

#include "vk_hpp.h"

namespace yr::render::vk {

  inline constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

  struct QueueFamilySelection {
    std::uint32_t graphics = kInvalidIndex;
    std::uint32_t present = kInvalidIndex;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool sameFamily() const noexcept;
  };

  struct PhysicalDeviceSelection {
    std::uint32_t deviceIndex = kInvalidIndex;
    QueueFamilySelection queues{};
    [[nodiscard]] bool valid() const noexcept;
  };

  // 纯决策：只读设备属性并打日志，不创建 Vulkan 对象。rules 写在实现处。
  [[nodiscard]] PhysicalDeviceSelection select_physical_device(std::span<const ::vk::PhysicalDevice> devices,
                                                               ::vk::SurfaceKHR surface);

} // namespace yr::render::vk