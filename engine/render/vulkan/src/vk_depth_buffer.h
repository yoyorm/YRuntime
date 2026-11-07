#pragma once

#include "vk_hpp.h"

namespace yr::render::vk {

  class Device;

  class DepthBuffer {
  public:
    DepthBuffer(const Device& device, ::vk::Extent2D extent, ::vk::Format format);
    ~DepthBuffer() = default;
    DepthBuffer(const DepthBuffer&) = delete;
    DepthBuffer& operator=(const DepthBuffer&) = delete;
    DepthBuffer(DepthBuffer&&) noexcept = default;
    DepthBuffer& operator=(DepthBuffer&&) noexcept = default;

    [[nodiscard]] ::vk::ImageView imageView() const noexcept { return *view_; }
    [[nodiscard]] ::vk::Image image() const noexcept { return *image_; }
    [[nodiscard]] ::vk::Format format() const noexcept { return format_; }

  private:
    ::vk::raii::DeviceMemory memory_{nullptr};
    ::vk::raii::Image image_{nullptr};
    ::vk::raii::ImageView view_{nullptr};
    ::vk::Format format_{};
  };

} // namespace yr::render::vk
