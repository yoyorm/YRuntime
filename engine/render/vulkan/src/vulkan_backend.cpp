#include <yr/render/vk/vulkan_backend.h>

#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include <yr/core/log.h>

#include "platform_window.h"
#include "vk_command_pool.h"
#include "vk_device.h"
#include "vk_frame_sync.h"
#include "vk_instance.h"
#include "vk_surface.h"
#include "vk_swapchain.h"
#include "vk_triangle_pipeline.h"

namespace yr::render {

  namespace {

    constexpr std::array<float, 4> kClearColor{0.10f, 0.18f, 0.30f, 1.0f};
    // CPU 最多同时在飞的帧数；fence / imageAvailable / command buffer 都按它分配。
    constexpr std::uint32_t kFramesInFlight = 2;

  } // namespace

  // M6 骨架的资源组合；声明顺序即依赖顺序（window → instance → surface → device → swapchain →
  // trianglePipeline → commandPool → frameSync），析构逆序。
  struct VulkanBackend::Impl {
    std::optional<vk::Window> window;
    std::optional<vk::Instance> instance;
    // 依赖 instance + window，故声明在二者之后、先于二者重置。
    std::optional<vk::Surface> surface;
    // 依赖 instance + surface，故声明在二者之后、先于二者重置。
    std::optional<vk::Device> device;
    // 依赖 device + surface + window，故声明在二者之后、先于三者重置。
    std::optional<vk::Swapchain> swapchain;
    // 依赖 device + swapchain format，故声明在 swapchain 之后、先于 swapchain 重置。
    std::optional<vk::TrianglePipeline> trianglePipeline;
    // 依赖 device，故声明在 device 之后、先于 device 重置。
    std::optional<vk::CommandPool> commandPool;
    std::optional<vk::FrameSync> frameSync;
    // 当前帧使用的 slot；提交后才推进，acquire 的 image index 是另一套索引。
    std::uint32_t frameSlot_ = 0;
    BackendStats stats{};
    std::uint32_t lastValidationErrors = 0;

    void shutdown() {
      if (!instance.has_value()) {
        return;
      }
      // 先等 GPU 完成：fence / semaphore / command buffer 不能在提交仍 pending 时销毁。
      if (device.has_value()) {
        device->raiiDevice().waitIdle();
      }
      frameSync.reset();
      commandPool.reset();
      trianglePipeline.reset();                                 // 依赖 swapchain format，先于 swapchain 释放
      swapchain.reset();                                        // 依赖 device，先于 device 释放
      device.reset();                                           // 依赖 surface，先于 surface 释放
      surface.reset();                                          // 可能产生 validation 消息
      const vk::DebugStats debugStats = instance->debugStats(); // 现在再取，包含 surface 析构期
      lastValidationErrors = debugStats.errors;
      instance.reset();
      window.reset();
      stats = BackendStats{};

      YR_LOG_INFO("render.vk.backend", "shutdown validation_errors=" + std::to_string(debugStats.errors) +
                                           " validation_warnings=" + std::to_string(debugStats.warnings));
    }

    // 重建失败只记录并保持旧链；调用方据此跳过本帧。
    bool tryRecreate() {
      try {
        // 旧 FrameSync 的 fence / semaphore 可能仍被 pending 提交引用，重建前必须等 GPU 空闲。
        device->raiiDevice().waitIdle();
        swapchain->recreate(*device, *surface, *window);
        // format 可能随新 swapchain 变化，pipeline 无条件按新 format 重建。
        // 边界：swapchain 已换新而 TrianglePipeline / FrameSync 构造失败时，optional 为空（swapchain 有效）。
        // 此时 renderFrame 直接返回，present 发现 pipeline 或 frameSync 缺失会再调一次本函数恢复。
        // 若 swapchain->recreate 抛异常，旧 pipeline 仍与旧 swapchain 匹配。
        trianglePipeline.emplace(*device, swapchain->format());
        // imageCount 可能变化，renderFinished 的数量随之变化，因此 FrameSync 与 swapchain 一起重建。
        frameSync.emplace(*device, kFramesInFlight, swapchain->imageCount());
        frameSlot_ = 0;
        YR_LOG_INFO("render.vk.backend", "swapchain 已重建 " + std::to_string(swapchain->extent().width) + "x" +
                                             std::to_string(swapchain->extent().height) +
                                             " imageCount=" + std::to_string(swapchain->imageCount()));
        return true;
      } catch (const std::exception& error) {
        YR_LOG_ERROR("render.vk.backend", std::string("swapchain 重建失败：") + error.what());
        return false;
      }
    }

    // N 帧在飞：等本 slot 的 fence → acquire → reset fence → 录制清屏 → submit → present → 推进 slot。
    void renderFrame() {
      if (!window.has_value() || !surface.has_value() || !device.has_value() || !swapchain.has_value() ||
          !trianglePipeline.has_value() || !commandPool.has_value() || !frameSync.has_value()) {
        return;
      }

      const std::uint32_t slot = frameSlot_;
      const ::vk::raii::Device& raiiDevice = device->raiiDevice();
      const ::vk::Result waitResult =
          raiiDevice.waitForFences(frameSync->inFlight(slot), ::vk::True, std::numeric_limits<std::uint64_t>::max());
      if (waitResult != ::vk::Result::eSuccess) {
        YR_LOG_ERROR("render.vk.backend", "waitForFences 失败：" + ::vk::to_string(waitResult));
        return;
      }

      const ::vk::ResultValue<std::uint32_t> acquired = swapchain->raiiHandle().acquireNextImage(
          std::numeric_limits<std::uint64_t>::max(), frameSync->imageAvailable(slot), nullptr);
      if (acquired.result == ::vk::Result::eErrorOutOfDateKHR) {
        // 宏已把 OOD 当成功返回；fence 未 reset，slot 不推进，下一帧仍可等待它。
        (void)tryRecreate();
        return;
      }
      if (acquired.result != ::vk::Result::eSuccess && acquired.result != ::vk::Result::eSuboptimalKHR) {
        YR_LOG_ERROR("render.vk.backend", "acquireNextImage 失败：" + ::vk::to_string(acquired.result));
        return;
      }

      raiiDevice.resetFences(frameSync->inFlight(slot));

      const bool acquiredSuboptimal = acquired.result == ::vk::Result::eSuboptimalKHR;
      const std::uint32_t imageIndex = acquired.value;
      const ::vk::Image image = swapchain->raiiHandle().getImages()[imageIndex];
      const ::vk::Extent2D extent = swapchain->extent();

      const ::vk::CommandBuffer cmd = commandPool->commandBuffer(slot);
      cmd.reset();
      cmd.begin(::vk::CommandBufferBeginInfo{});

      const ::vk::ImageSubresourceRange colorRange{::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

      const ::vk::ImageMemoryBarrier2 toColorAttachment{::vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                                        ::vk::AccessFlagBits2::eNone,
                                                        ::vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                                        ::vk::AccessFlagBits2::eColorAttachmentWrite,
                                                        ::vk::ImageLayout::eUndefined,
                                                        ::vk::ImageLayout::eColorAttachmentOptimal,
                                                        ::vk::QueueFamilyIgnored,
                                                        ::vk::QueueFamilyIgnored,
                                                        image,
                                                        colorRange};
      ::vk::DependencyInfo dependency{};
      dependency.setImageMemoryBarriers(toColorAttachment);
      cmd.pipelineBarrier2(dependency);

      ::vk::RenderingAttachmentInfo colorAttachment{};
      colorAttachment.setImageView(swapchain->imageView(imageIndex))
          .setImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
          .setLoadOp(::vk::AttachmentLoadOp::eClear)
          .setStoreOp(::vk::AttachmentStoreOp::eStore)
          .setClearValue(::vk::ClearValue{::vk::ClearColorValue{kClearColor}});

      ::vk::RenderingInfo renderingInfo{};
      renderingInfo.setRenderArea(::vk::Rect2D{{0, 0}, extent}).setLayerCount(1).setColorAttachments(colorAttachment);
      cmd.beginRendering(renderingInfo);
      cmd.bindPipeline(::vk::PipelineBindPoint::eGraphics, trianglePipeline->handle());
      cmd.setViewport(0, ::vk::Viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
                                        0.0f, 1.0f});
      cmd.setScissor(0, ::vk::Rect2D{{0, 0}, extent});
      cmd.draw(3, 1, 0, 0);
      cmd.endRendering();

      const ::vk::ImageMemoryBarrier2 toPresent{::vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                                ::vk::AccessFlagBits2::eColorAttachmentWrite,
                                                ::vk::PipelineStageFlagBits2::eNone,
                                                ::vk::AccessFlagBits2::eNone,
                                                ::vk::ImageLayout::eColorAttachmentOptimal,
                                                ::vk::ImageLayout::ePresentSrcKHR,
                                                ::vk::QueueFamilyIgnored,
                                                ::vk::QueueFamilyIgnored,
                                                image,
                                                colorRange};
      dependency.setImageMemoryBarriers(toPresent);
      cmd.pipelineBarrier2(dependency);

      cmd.end();

      ::vk::SemaphoreSubmitInfo waitSemaphore{};
      waitSemaphore.setSemaphore(frameSync->imageAvailable(slot))
          .setStageMask(::vk::PipelineStageFlagBits2::eColorAttachmentOutput);
      ::vk::CommandBufferSubmitInfo commandBufferInfo{};
      commandBufferInfo.setCommandBuffer(cmd);
      // 按 image 索引 signal：同一 image 再被 acquire 前，它的 present 必然已完成。
      ::vk::SemaphoreSubmitInfo signalSemaphore{};
      signalSemaphore.setSemaphore(frameSync->renderFinished(imageIndex))
          .setStageMask(::vk::PipelineStageFlagBits2::eColorAttachmentOutput);

      ::vk::SubmitInfo2 submitInfo{};
      submitInfo.setWaitSemaphoreInfos(waitSemaphore)
          .setCommandBufferInfos(commandBufferInfo)
          .setSignalSemaphoreInfos(signalSemaphore);
      device->graphicsQueue().submit2(submitInfo, frameSync->inFlight(slot));

      const ::vk::SwapchainKHR swapchainHandle = swapchain->handle();
      const ::vk::Semaphore renderFinished = frameSync->renderFinished(imageIndex);
      const ::vk::PresentInfoKHR presentInfo{1, &renderFinished, 1, &swapchainHandle, &imageIndex, nullptr, nullptr};
      const ::vk::Result presentResult = device->presentQueue().presentKHR(presentInfo);

      // 提交之后本 slot 的 fence / semaphore 已交给 GPU：无论 present 结果如何都可以换下一个 slot。
      frameSlot_ = (frameSlot_ + 1) % frameSync->framesInFlight();

      if (presentResult != ::vk::Result::eSuccess && presentResult != ::vk::Result::eSuboptimalKHR &&
          presentResult != ::vk::Result::eErrorOutOfDateKHR) {
        YR_LOG_ERROR("render.vk.backend", "presentKHR 失败：" + ::vk::to_string(presentResult));
        return;
      }

      // acquire / present 任一报 OOD 或 Suboptimal：本帧已结束，重建留给下一次。
      if (acquiredSuboptimal || presentResult == ::vk::Result::eErrorOutOfDateKHR ||
          presentResult == ::vk::Result::eSuboptimalKHR) {
        (void)tryRecreate();
      }
    }
  };

  VulkanBackend::VulkanBackend() : impl_(std::make_unique<Impl>()) {}

  VulkanBackend::~VulkanBackend() {
    shutdown();
  }

  bool VulkanBackend::initialize(const BackendConfig& /*config*/) {
    try {
      // 窗口必须先于 instance：glfwGetRequiredInstanceExtensions 依赖 glfwInit。
      impl_->window.emplace();
      impl_->instance.emplace();
      impl_->surface.emplace(*impl_->instance, *impl_->window);
      impl_->device.emplace(*impl_->instance, *impl_->surface);
      impl_->swapchain.emplace(*impl_->device, *impl_->surface, *impl_->window);
      impl_->trianglePipeline.emplace(*impl_->device, impl_->swapchain->format());
      impl_->commandPool.emplace(*impl_->device, kFramesInFlight);
      impl_->frameSync.emplace(*impl_->device, kFramesInFlight, impl_->swapchain->imageCount());
    } catch (const std::exception& error) {
      YR_LOG_ERROR("render.vk.backend", std::string("初始化失败：") + error.what());
      impl_->lastValidationErrors = 0;
      impl_->frameSync.reset();
      impl_->commandPool.reset();
      impl_->trianglePipeline.reset();
      impl_->swapchain.reset();
      impl_->device.reset();
      impl_->surface.reset();
      impl_->instance.reset();
      impl_->window.reset();
      return false;
    }
    impl_->stats = BackendStats{};
    YR_LOG_INFO("render.vk.backend", "初始化完成 framesInFlight=" + std::to_string(kFramesInFlight) +
                                         " imageCount=" + std::to_string(impl_->swapchain->imageCount()));
    return true;
  }

  void VulkanBackend::shutdown() {
    impl_->shutdown();
  }

  void VulkanBackend::present(const RenderSnapshot& /*snapshot*/) {
    // M6-4 仍不消费快照：清屏是固定颜色的固定链路（M7 接快照）。
    if (!impl_->window.has_value()) {
      return;
    }
    impl_->window->pollEvents();
    if (!impl_->swapchain.has_value() || !impl_->device.has_value() || !impl_->surface.has_value() ||
        !impl_->commandPool.has_value()) {
      return;
    }

    // 最小化时 framebuffer 为 0×0：跳过，等恢复正常尺寸后再重建。
    if (impl_->window->width() <= 0 || impl_->window->height() <= 0) {
      return;
    }
    if (!impl_->frameSync.has_value() || !impl_->trianglePipeline.has_value()) {
      // 上一次重建在 TrianglePipeline / FrameSync 构造处失败（swapchain 已是新的）：再走一次重建以恢复出帧。
      (void)impl_->tryRecreate();
      return;
    }

    // 期望 extent 由 surface 能力现算，不拿窗口像素尺寸直接比 extent。
    if (impl_->swapchain->needsRecreate(*impl_->device, *impl_->surface, *impl_->window)) {
      if (!impl_->tryRecreate()) {
        return;
      }
    }
    impl_->renderFrame();
  }

  const char* VulkanBackend::name() const noexcept {
    return "vulkan";
  }

  BackendStats VulkanBackend::last_frame_stats() const noexcept {
    return impl_->stats;
  }

  bool VulkanBackend::shouldClose() const noexcept {
    if (!impl_->window.has_value()) {
      return true;
    }
    return impl_->window->shouldClose();
  }

  std::uint32_t VulkanBackend::validationErrorCount() const noexcept {
    if (impl_->instance.has_value()) {
      return impl_->instance->debugStats().errors;
    }
    return impl_->lastValidationErrors;
  }

  std::unique_ptr<RendererBackend> create_vulkan_backend() {
    return std::make_unique<VulkanBackend>();
  }

} // namespace yr::render