#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vk_hpp.h"

namespace yr::render::vk {

  class Device;
  class Surface;
  class Window;

  // VkSwapchainKHR + image views 的所有者；依赖 device + surface + window，必须先于三者销毁。
  // 不拥有 depth 资源（M6-3a 只建 color attachment 用途的 swapchain）。
  // images 在创建/重建时取一次并缓存（非拥有，句柄归 swapchain），避免每帧 getImages() 分配。
  class Swapchain {
  public:
    Swapchain(const Device& device, const Surface& surface, const Window& window);
    ~Swapchain() = default;
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    // 顺序契约：以旧 handle 建新 → 建新 views → 新 views 替换旧 views（旧 views 死）→ 新 swapchain 替换旧
    // swapchain（旧 handle 死）。任何一步抛异常，旧 swapchain / views 保持完整。
    void recreate(const Device& device, const Surface& surface, const Window& window);

    // surface 能力现查，期望 extent 与当前 extent 不一致即需重建；0×0 由调用方先行跳过。
    [[nodiscard]] bool needsRecreate(const Device& device, const Surface& surface, const Window& window) const;

    [[nodiscard]] ::vk::SwapchainKHR handle() const noexcept { return *swapchain_; }
    [[nodiscard]] const ::vk::raii::SwapchainKHR& raiiHandle() const noexcept { return swapchain_; }
    [[nodiscard]] ::vk::Format format() const noexcept { return format_; }
    [[nodiscard]] ::vk::Extent2D extent() const noexcept { return extent_; }
    [[nodiscard]] std::uint32_t imageCount() const noexcept { return static_cast<std::uint32_t>(images_.size()); }
    [[nodiscard]] ::vk::Image image(std::uint32_t index) const;
    [[nodiscard]] ::vk::ImageView imageView(std::uint32_t index) const;

  private:
    struct Created {
      ::vk::raii::SwapchainKHR handle{nullptr};
      std::vector<::vk::Image> images;
      ::vk::Format format{};
      ::vk::Extent2D extent{};
    };

    // 只查询能力并创建对象，不修改成员；oldSwapchain 为空表示首次创建。
    [[nodiscard]] Created create(const Device& device, const Surface& surface, const Window& window,
                                 ::vk::SwapchainKHR oldSwapchain) const;
    [[nodiscard]] std::vector<::vk::raii::ImageView>
    buildImageViews(const Device& device, std::span<const ::vk::Image> images, ::vk::Format format) const;

    ::vk::raii::SwapchainKHR swapchain_{nullptr};
    std::vector<::vk::Image> images_; // 非拥有；句柄由 swapchain_ 拥有
    std::vector<::vk::raii::ImageView> imageViews_;
    ::vk::Format format_{};
    ::vk::Extent2D extent_{};
  };

} // namespace yr::render::vk