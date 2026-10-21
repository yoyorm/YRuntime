#include <yr/render/vk/vulkan_backend.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>

#include <yr/core/log.h>

#include "platform_window.h"
#include "vk_device.h"
#include "vk_instance.h"
#include "vk_surface.h"

namespace yr::render {

  // M6 骨架的资源组合；声明顺序即依赖顺序（window → instance → surface → device），析构逆序。
  struct VulkanBackend::Impl {
    std::optional<vk::Window> window;
    std::optional<vk::Instance> instance;
    // 依赖 instance + window，故声明在二者之后、先于二者重置。
    std::optional<vk::Surface> surface;
    // 依赖 instance + surface，故声明在二者之后、先于二者重置。
    std::optional<vk::Device> device;
    BackendStats stats{};
    std::uint32_t lastValidationErrors = 0;

    void shutdown() {
      if (!instance.has_value()) {
        return;
      }
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
    } catch (const std::exception& error) {
      YR_LOG_ERROR("render.vk.backend", std::string("初始化失败：") + error.what());
      impl_->lastValidationErrors = 0;
      impl_->device.reset();
      impl_->surface.reset();
      impl_->instance.reset();
      impl_->window.reset();
      return false;
    }
    impl_->stats = BackendStats{};
    YR_LOG_INFO("render.vk.backend", "初始化完成");
    return true;
  }

  void VulkanBackend::shutdown() {
    impl_->shutdown();
  }

  void VulkanBackend::present(const RenderSnapshot& /*snapshot*/) {
    // M6 不消费快照：只驱动窗口事件。
    if (impl_->window.has_value()) {
      impl_->window->pollEvents();
    }
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