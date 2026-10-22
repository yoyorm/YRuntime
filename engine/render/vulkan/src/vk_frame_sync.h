#pragma once

#include <cstdint>
#include <vector>

#include "vk_hpp.h"

namespace yr::render::vk {

  class Device;

  // 同步原语的所有者；依赖 device，必须先于 device 销毁。
  // 两种粒度：
  // - imageAvailable / inFlight 按 frame slot 分配（framesInFlight 个）。同一个 slot 的下一次使用
  //   必然发生在 CPU 已 wait 过它的 fence 之后，因此复用安全。
  // - renderFinished 按 swapchain image 索引分配（imageCount 个）。同一个 image 只有在它的 present
  //   完成后才会被再次 acquire，所以该 semaphore 的下一次 signal 必然发生在旧的 wait 被消费之后。
  //   按 slot 复用只是"实践上成立"的假设：规范不保证 present 的 wait 一定先于下一次 submit 的 signal。
  // swapchain 重建可能改变 imageCount，因此 FrameSync 必须与 swapchain 一起重建。
  class FrameSync {
  public:
    FrameSync(const Device& device, std::uint32_t framesInFlight, std::uint32_t imageCount);
    ~FrameSync() = default;
    FrameSync(const FrameSync&) = delete;
    FrameSync& operator=(const FrameSync&) = delete;
    FrameSync(FrameSync&&) = delete;
    FrameSync& operator=(FrameSync&&) = delete;

    [[nodiscard]] std::uint32_t framesInFlight() const noexcept { return static_cast<std::uint32_t>(inFlight_.size()); }
    // 越界是编程错误：debug 下断言，release 下不检查。
    [[nodiscard]] ::vk::Semaphore imageAvailable(std::uint32_t slot) const;
    [[nodiscard]] ::vk::Fence inFlight(std::uint32_t slot) const;
    [[nodiscard]] ::vk::Semaphore renderFinished(std::uint32_t imageIndex) const;

  private:
    std::vector<::vk::raii::Semaphore> imageAvailable_; // size = framesInFlight
    std::vector<::vk::raii::Semaphore> renderFinished_; // size = imageCount
    // 全部创建为 signaled：第一轮各 slot 不必先有人 signal 就能通过 waitForFences。
    std::vector<::vk::raii::Fence> inFlight_; // size = framesInFlight
  };

} // namespace yr::render::vk