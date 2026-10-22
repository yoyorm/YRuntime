#include "vk_swapchain.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <yr/core/assert.h>
#include <yr/core/log.h>

#include "platform_window.h"
#include "vk_device.h"
#include "vk_surface.h"

namespace yr::render::vk {

  namespace {

    constexpr ::vk::Format kPreferredFormat = ::vk::Format::eB8G8R8A8Srgb;
    constexpr ::vk::ColorSpaceKHR kPreferredColorSpace = ::vk::ColorSpaceKHR::eSrgbNonlinear;

    // 优先 eOpaque；否则按 eInherit → ePreMultiplied → ePostMultiplied 取第一个受支持的。
    [[nodiscard]] ::vk::CompositeAlphaFlagBitsKHR selectCompositeAlpha(::vk::CompositeAlphaFlagsKHR supported) {
      if (supported & ::vk::CompositeAlphaFlagBitsKHR::eOpaque) {
        return ::vk::CompositeAlphaFlagBitsKHR::eOpaque;
      }
      constexpr std::array<::vk::CompositeAlphaFlagBitsKHR, 3> kFallbacks{
          ::vk::CompositeAlphaFlagBitsKHR::eInherit,
          ::vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
          ::vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
      };
      for (const ::vk::CompositeAlphaFlagBitsKHR candidate : kFallbacks) {
        if (supported & candidate) {
          return candidate;
        }
      }
      throw std::runtime_error("surface 不支持任何 composite alpha");
    }

    // create 与 needsRecreate 共用的唯一 extent 决策：平台给定时直接用；否则窗口帧缓冲尺寸 clamp。
    [[nodiscard]] ::vk::Extent2D computeExtent(const ::vk::SurfaceCapabilitiesKHR& caps, const Window& window) {
      if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return caps.currentExtent;
      }
      ::vk::Extent2D extent{};
      extent.width =
          std::clamp(static_cast<std::uint32_t>(window.width()), caps.minImageExtent.width, caps.maxImageExtent.width);
      extent.height = std::clamp(static_cast<std::uint32_t>(window.height()), caps.minImageExtent.height,
                                 caps.maxImageExtent.height);
      return extent;
    }

  } // namespace

  Swapchain::Swapchain(const Device& device, const Surface& surface, const Window& window) {
    Created created = create(device, surface, window, nullptr);
    std::vector<::vk::raii::ImageView> views = buildImageViews(device, created.images, created.format);
    swapchain_ = std::move(created.handle);
    images_ = std::move(created.images);
    imageViews_ = std::move(views);
    format_ = created.format;
    extent_ = created.extent;
  }

  Swapchain::Created Swapchain::create(const Device& device, const Surface& surface, const Window& window,
                                       ::vk::SwapchainKHR oldSwapchain) const {
    // 1. surface 能力：全部从 physical device 现查，不复用上一次的缓存。
    const ::vk::PhysicalDevice physicalDevice = device.physicalDevice();
    const ::vk::SurfaceCapabilitiesKHR caps = physicalDevice.getSurfaceCapabilitiesKHR(surface.handle());
    const std::vector<::vk::SurfaceFormatKHR> formats = physicalDevice.getSurfaceFormatsKHR(surface.handle());
    const std::vector<::vk::PresentModeKHR> presentModes = physicalDevice.getSurfacePresentModesKHR(surface.handle());

    // 2. format：优先 {B8G8R8A8Srgb, SrgbNonlinear}；不在列表则取 formats[0]。
    if (formats.empty()) {
      throw std::runtime_error("surface 未提供任何 swapchain 格式");
    }
    ::vk::SurfaceFormatKHR surfaceFormat = formats.front();
    for (const ::vk::SurfaceFormatKHR& candidate : formats) {
      if (candidate.format == kPreferredFormat && candidate.colorSpace == kPreferredColorSpace) {
        surfaceFormat = candidate;
        break;
      }
    }

    // 3. present mode：优先 eMailbox，否则 eFifo（FIFO 由规范保证支持）。
    if (presentModes.empty()) {
      throw std::runtime_error("surface 未提供任何 present mode");
    }
    const bool hasMailbox =
        std::find(presentModes.begin(), presentModes.end(), ::vk::PresentModeKHR::eMailbox) != presentModes.end();
    const ::vk::PresentModeKHR presentMode = hasMailbox ? ::vk::PresentModeKHR::eMailbox : ::vk::PresentModeKHR::eFifo;

    // 4. extent：与 needsRecreate 共用 computeExtent。
    const ::vk::Extent2D extent = computeExtent(caps, window);
    // 最小化时 framebuffer 为 0×0；调用方（M6-3b）负责跳过本帧。
    if (extent.width == 0 || extent.height == 0) {
      throw std::runtime_error("窗口最小化，无法创建 swapchain");
    }

    // 5. imageCount：minImageCount + 1，maxImageCount != 0 时 clamp。
    std::uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount != 0) {
      imageCount = std::min(imageCount, caps.maxImageCount);
    }

    // 6. sharing：同族用 eExclusive 且不传族索引；异族用 eConcurrent + 两个索引。
    const QueueFamilySelection& queues = device.queues();
    const bool concurrent = !queues.sameFamily();
    const std::uint32_t concurrentIndices[2] = {queues.graphics, queues.present};
    const ::vk::SharingMode sharingMode = concurrent ? ::vk::SharingMode::eConcurrent : ::vk::SharingMode::eExclusive;
    const std::uint32_t queueFamilyIndexCount = concurrent ? 2u : 0u;
    const std::uint32_t* queueFamilyIndices = concurrent ? concurrentIndices : nullptr;

    // 7/8/9. preTransform 跟随 surface；clipped 恒开；usage 只有 color attachment，不建 depth。
    // 全参数构造函数：避免默认 imageArrayLayers = 0 / presentMode = eImmediate 等陷阱。
    const ::vk::SwapchainCreateInfoKHR createInfo(
        {}, surface.handle(), imageCount, surfaceFormat.format, surfaceFormat.colorSpace, extent, 1u,
        ::vk::ImageUsageFlagBits::eColorAttachment, sharingMode, queueFamilyIndexCount, queueFamilyIndices,
        caps.currentTransform, selectCompositeAlpha(caps.supportedCompositeAlpha), presentMode, ::vk::True,
        oldSwapchain, nullptr);

    ::vk::raii::SwapchainKHR swapchain(device.raiiDevice(), createInfo);
    std::vector<::vk::Image> images = swapchain.getImages();

    // 12. 一行日志记录本次选择。
    YR_LOG_INFO("render.vk.swapchain", "swapchain 创建成功 format=" + ::vk::to_string(surfaceFormat.format) +
                                           " extent=" + std::to_string(extent.width) + "x" +
                                           std::to_string(extent.height) + " imageCount=" + std::to_string(imageCount) +
                                           " presentMode=" + ::vk::to_string(presentMode) +
                                           " sharing=" + (concurrent ? "concurrent" : "exclusive"));

    return Created{std::move(swapchain), std::move(images), surfaceFormat.format, extent};
  }

  // 11. 每张 image 一个 e2D color view，components 用默认（identity）。
  std::vector<::vk::raii::ImageView>
  Swapchain::buildImageViews(const Device& device, std::span<const ::vk::Image> images, ::vk::Format format) const {
    std::vector<::vk::raii::ImageView> views;
    views.reserve(images.size());
    for (const ::vk::Image image : images) {
      const ::vk::ImageViewCreateInfo createInfo(
          {}, image, ::vk::ImageViewType::e2D, format, {},
          ::vk::ImageSubresourceRange{::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
      views.emplace_back(device.raiiDevice(), createInfo);
    }
    return views;
  }

  void Swapchain::recreate(const Device& device, const Surface& surface, const Window& window) {
    // 全部成功前不改成员：create / buildImageViews 抛异常时旧链保持完整。
    Created next = create(device, surface, window, *swapchain_);
    std::vector<::vk::raii::ImageView> nextViews = buildImageViews(device, next.images, next.format);
    // 旧 views 先死，此时旧 swapchain 还活着。
    imageViews_ = std::move(nextViews);
    // 旧 swapchain 死于赋值；随后才更新 images / format / extent。
    swapchain_ = std::move(next.handle);
    images_ = std::move(next.images);
    format_ = next.format;
    extent_ = next.extent;
  }

  bool Swapchain::needsRecreate(const Device& device, const Surface& surface, const Window& window) const {
    const ::vk::SurfaceCapabilitiesKHR caps = device.physicalDevice().getSurfaceCapabilitiesKHR(surface.handle());
    const ::vk::Extent2D expected = computeExtent(caps, window);
    return expected.width != extent_.width || expected.height != extent_.height;
  }

  ::vk::Image Swapchain::image(std::uint32_t index) const {
    YR_ASSERT(index < images_.size());
    return images_[index];
  }

  ::vk::ImageView Swapchain::imageView(std::uint32_t index) const {
    YR_ASSERT(index < imageViews_.size());
    return *imageViews_[index];
  }

} // namespace yr::render::vk