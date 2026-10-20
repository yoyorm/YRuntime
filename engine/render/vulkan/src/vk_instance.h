#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include "vk_debug.h"

namespace yr::render::vk {

  // VkInstance 的所有者；validation 层 + debug messenger 仅在
  // YR_ENABLE_VULKAN_VALIDATION（Debug 构建）下启用。

  class Instance {
  public:
    Instance();
    ~Instance() = default;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    Instance(Instance&&) = delete;
    Instance& operator=(Instance&&) = delete;

    [[nodiscard]] ::vk::Instance handle() const noexcept { return *instance_; }
    [[nodiscard]] const DebugStats& debugStats() const noexcept { return debugStats_; }
    // 本次构建是否真的启用了 validation 层（探测到 + 已列入启用层）。
    [[nodiscard]] bool validationEnabled() const noexcept { return validationEnabled_; }
    // loader 是否暴露 VK_LAYER_KHRONOS_validation；validation 被编译掉时恒为 false。
    [[nodiscard]] bool validationLayerAvailable() const noexcept { return validationLayerAvailable_; }
    [[nodiscard]] const std::vector<std::string>& enabledLayers() const noexcept { return enabledLayers_; }
    [[nodiscard]] const std::vector<std::string>& enabledExtensions() const noexcept { return enabledExtensions_; }

  private:
    ::vk::raii::Context context_;
    DebugStats debugStats_{};
    bool validationLayerAvailable_ = false;
    bool validationEnabled_ = false;
    std::vector<std::string> enabledLayers_;
    std::vector<std::string> enabledExtensions_;
    ::vk::raii::Instance instance_{nullptr};
    ::vk::raii::DebugUtilsMessengerEXT debugMessenger_{nullptr};
  };

} // namespace yr::render::vk