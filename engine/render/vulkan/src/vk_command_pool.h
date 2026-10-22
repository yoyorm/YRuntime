#pragma once

#include <cstdint>
#include <vector>

#include "vk_hpp.h"

namespace yr::render::vk {

  class Device;

  // VkCommandPool + bufferCount 条 primary command buffer 的所有者；依赖 device，必须先于 device 销毁。
  // buffer 数与 framesInFlight 一致：每个 frame slot 一条，按 slot 取，每帧 reset 后重录。
  class CommandPool {
  public:
    CommandPool(const Device& device, std::uint32_t bufferCount);
    ~CommandPool() = default;
    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;
    CommandPool(CommandPool&&) = delete;
    CommandPool& operator=(CommandPool&&) = delete;

    // 越界是编程错误：debug 下断言，release 下不检查。
    [[nodiscard]] ::vk::CommandBuffer commandBuffer(std::uint32_t slot) const;

  private:
    ::vk::raii::CommandPool pool_{nullptr};
    std::vector<::vk::raii::CommandBuffer> commandBuffers_;
  };

} // namespace yr::render::vk