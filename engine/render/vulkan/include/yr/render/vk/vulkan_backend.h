#pragma once

#include <cstdint>
#include <memory>

#include <yr/render/backend.h>

namespace yr::render {

  // host 层按键枚举：只覆盖 demo 需要的移动键，不进入 RendererBackend 契约。
  enum class Key : std::uint8_t { W, A, S, D };

  // M6 骨架：拥有窗口与 VkInstance；present 只负责渲染、不消费快照。
  // 本头文件刻意保持 vulkan-free / GLFW-free（PIMPL）。
  class VulkanBackend final : public RendererBackend {
  public:
    VulkanBackend();
    ~VulkanBackend() override;
    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;
    VulkanBackend(VulkanBackend&&) = delete;
    VulkanBackend& operator=(VulkanBackend&&) = delete;

    bool initialize(const BackendConfig& config) override;
    void shutdown() override;
    void present(const RenderSnapshot& snapshot) override;
    [[nodiscard]] const char* name() const noexcept override;
    [[nodiscard]] BackendStats last_frame_stats() const noexcept override;

    // 由 host 在帧阶段 1（poll_input）调用；present() 不再泵事件。
    void pollEvents();

    // host 级平台查询：窗口是否请求关闭。M7 由 DemoLoop::pollInput 消费后调 requestQuit()。
    // 不属于 RendererBackend 契约。
    [[nodiscard]] bool shouldClose() const noexcept;

    // host 级平台查询：按键当前是否按下。先 pollEvents() 再查询，状态才与本次事件泵同步。
    // 不属于 RendererBackend 契约。
    [[nodiscard]] bool isKeyDown(Key key) const noexcept;

    // 本次运行累计的 validation error 数（Debug 才有意义；Release 编译掉时恒 0）。
    [[nodiscard]] std::uint32_t validationErrorCount() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

  [[nodiscard]] std::unique_ptr<RendererBackend> create_vulkan_backend();

} // namespace yr::render