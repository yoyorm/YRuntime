#include "vk_command_pool.h"

#include <yr/core/assert.h>

#include "vk_device.h"

namespace yr::render::vk {

  CommandPool::CommandPool(const Device& device, std::uint32_t bufferCount) {
    YR_ASSERT(bufferCount > 0);

    const ::vk::CommandPoolCreateInfo createInfo{::vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                                 device.queues().graphics};
    pool_ = ::vk::raii::CommandPool(device.raiiDevice(), createInfo);

    const ::vk::CommandBufferAllocateInfo allocateInfo{pool_, ::vk::CommandBufferLevel::ePrimary, bufferCount};
    commandBuffers_ = device.raiiDevice().allocateCommandBuffers(allocateInfo);
  }

  ::vk::CommandBuffer CommandPool::commandBuffer(std::uint32_t slot) const {
    YR_ASSERT(slot < commandBuffers_.size());
    return *commandBuffers_[slot];
  }

} // namespace yr::render::vk