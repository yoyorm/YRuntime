#pragma once

#include <vulkan/vulkan_raii.hpp>

#include "vk_device_selection.h"

namespace yr::render::vk {

  class Instance;
  class Surface;

  // VkDevice 的所有者；依赖 instance + surface，必须先于二者销毁。
  class Device {
  public:
    Device(const Instance& instance, const Surface& surface);
    ~Device() = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    [[nodiscard]] ::vk::PhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
    [[nodiscard]] ::vk::Device handle() const noexcept { return *device_; }
    [[nodiscard]] ::vk::Queue graphicsQueue() const noexcept { return graphicsQueue_; }
    [[nodiscard]] ::vk::Queue presentQueue() const noexcept { return presentQueue_; }
    [[nodiscard]] const QueueFamilySelection& queues() const noexcept { return queues_; }

  private:
    ::vk::PhysicalDevice physicalDevice_{}; // 非拥有：物理设备没有 destroy
    ::vk::raii::Device device_{nullptr};
    ::vk::Queue graphicsQueue_{}; // 非 RAII：由 device 拥有
    ::vk::Queue presentQueue_{};
    QueueFamilySelection queues_{};
  };

} // namespace yr::render::vk