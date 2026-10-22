#include "vk_frame_sync.h"

#include <yr/core/assert.h>

#include "vk_device.h"

namespace yr::render::vk {

  FrameSync::FrameSync(const Device& device, std::uint32_t framesInFlight, std::uint32_t imageCount) {
    YR_ASSERT(framesInFlight > 0);
    YR_ASSERT(imageCount > 0);

    const ::vk::raii::Device& raiiDevice = device.raiiDevice();
    imageAvailable_.reserve(framesInFlight);
    for (std::uint32_t slot = 0; slot < framesInFlight; ++slot) {
      imageAvailable_.emplace_back(raiiDevice, ::vk::SemaphoreCreateInfo{});
    }
    renderFinished_.reserve(imageCount);
    for (std::uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
      renderFinished_.emplace_back(raiiDevice, ::vk::SemaphoreCreateInfo{});
    }
    inFlight_.reserve(framesInFlight);
    for (std::uint32_t slot = 0; slot < framesInFlight; ++slot) {
      inFlight_.emplace_back(raiiDevice, ::vk::FenceCreateInfo{::vk::FenceCreateFlagBits::eSignaled});
    }
  }

  ::vk::Semaphore FrameSync::imageAvailable(std::uint32_t slot) const {
    YR_ASSERT(slot < imageAvailable_.size());
    return *imageAvailable_[slot];
  }

  ::vk::Fence FrameSync::inFlight(std::uint32_t slot) const {
    YR_ASSERT(slot < inFlight_.size());
    return *inFlight_[slot];
  }

  ::vk::Semaphore FrameSync::renderFinished(std::uint32_t imageIndex) const {
    YR_ASSERT(imageIndex < renderFinished_.size());
    return *renderFinished_[imageIndex];
  }

} // namespace yr::render::vk