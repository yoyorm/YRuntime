#pragma once

#include <vector>

#include <yr/input/input_source.h>
#include <yr/render/vk/vulkan_backend.h>

namespace yr::demo {

  // 把 VulkanBackend 的 host 级 GLFW 原始事件转换成 yr::input::InputEvent。
  // 只在 poll() 里泵一次窗口事件；回调期间先写入 pending_，避免在 GLFW 回调里重入 Router。
  class GlfwInputSource final : public yr::input::IInputSource {
  public:
    explicit GlfwInputSource(yr::render::VulkanBackend& backend);
    ~GlfwInputSource() override;

    GlfwInputSource(const GlfwInputSource&) = delete;
    GlfwInputSource& operator=(const GlfwInputSource&) = delete;
    GlfwInputSource(GlfwInputSource&&) = delete;
    GlfwInputSource& operator=(GlfwInputSource&&) = delete;

    void poll(yr::input::IInputSink& sink) override;

    // 切换鼠标锁定模式后调用：丢弃下一帧的绝对坐标跳变，只保留之后的相对增量。
    void resetCursorTracking() noexcept { hasCursor_ = false; }

  private:
    void onKey(yr::render::Key key, yr::render::KeyAction action);
    void onMouseButton(yr::render::MouseButton button, yr::render::KeyAction action, double x, double y);
    void onCursor(double x, double y);
    void onScroll(double dx, double dy);
    void onCharacter(char32_t codepoint);

    yr::render::VulkanBackend& backend_;
    std::vector<yr::input::InputEvent> pending_;
    double lastX_ = 0.0;
    double lastY_ = 0.0;
    bool hasCursor_ = false;
  };

} // namespace yr::demo