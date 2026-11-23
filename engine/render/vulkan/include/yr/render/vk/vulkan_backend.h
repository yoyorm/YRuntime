#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <yr/render/backend.h>

namespace yr::render {

  // host 层按键枚举：覆盖常用操作键，不进入 RendererBackend 契约。
  enum class Key : std::uint8_t { W, A, S, D, Left, Right, Up, Down, Space, Escape, Enter, Unknown };

  enum class KeyAction : std::uint8_t { kPress, kRelease, kRepeat };

  enum class KeyMod : std::uint8_t {
    kShift = 1U << 0,
    kControl = 1U << 1,
    kAlt = 1U << 2,
    kSuper = 1U << 3,
  };
  using KeyMods = std::uint8_t;

  enum class MouseButton : std::uint8_t { kLeft, kRight, kMiddle, kOther };

  // host 级原始输入回调。由 app 注册，GLFW thunk 在 pollEvents 期间调用。
  // 不属于 RendererBackend 契约，公开头仍然 GLFW-free。
  struct InputCallbacks {
    std::function<void(Key, KeyAction, KeyMods)> key;
    std::function<void(MouseButton, KeyAction, KeyMods, double, double)> mouseButton;
    std::function<void(double, double)> cursorPos;
    std::function<void(double, double)> scroll;
    std::function<void(char32_t)> character;
  };

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

    // host 级平台控制：鼠标锁定/释放（GLFW cursor disabled/normal）。
    void setCursorCaptured(bool captured);
    [[nodiscard]] bool cursorCaptured() const noexcept;

    // host 级平台查询：窗口是否请求关闭。M7 由 DemoLoop::pollInput 消费后调 requestQuit()。
    // 不属于 RendererBackend 契约。
    [[nodiscard]] bool shouldClose() const noexcept;

    // host 级平台查询：按键当前是否按下。先 pollEvents() 再查询，状态才与本次事件泵同步。
    // 不属于 RendererBackend 契约。
    [[nodiscard]] bool isKeyDown(Key key) const noexcept;

    // host 级输入回调：在 initialize() 前后调用均可；窗口存在时自动安装 GLFW thunk。
    void setInputCallbacks(InputCallbacks callbacks);

    // 本次运行累计的 validation error 数（Debug 才有意义；Release 编译掉时恒 0）。
    [[nodiscard]] std::uint32_t validationErrorCount() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

  [[nodiscard]] std::unique_ptr<RendererBackend> create_vulkan_backend();

} // namespace yr::render