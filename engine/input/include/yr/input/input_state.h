#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

#include <yr/input/input_source.h>

namespace yr::input {

  // 原始输入状态汇总
  class InputState final : public IInputSink {
  public:
    void beginFrame();
    void submit(InputEvent event) override;

    [[nodiscard]] bool held(KeyCode code) const noexcept;
    [[nodiscard]] bool pressed(KeyCode code) const noexcept;
    [[nodiscard]] bool released(KeyCode code) const noexcept;
    [[nodiscard]] bool repeated(KeyCode code) const noexcept;

    [[nodiscard]] float mouseX() const noexcept { return mouseX_; }
    [[nodiscard]] float mouseY() const noexcept { return mouseY_; }
    [[nodiscard]] float mouseDeltaX() const noexcept { return mouseDeltaX_; }
    [[nodiscard]] float mouseDeltaY() const noexcept { return mouseDeltaY_; }

    [[nodiscard]] float scrollX() const noexcept { return scrollX_; }
    [[nodiscard]] float scrollY() const noexcept { return scrollY_; }

    [[nodiscard]] std::string_view text() const noexcept { return text_; }

  private:
    void applyButton(KeyCode code, KeyPhase phase);

    std::unordered_set<KeyCode> held_;
    std::unordered_set<KeyCode> pressed_;
    std::unordered_set<KeyCode> released_;
    std::unordered_set<KeyCode> repeated_;

    float mouseX_ = 0.0F;
    float mouseY_ = 0.0F;
    float mouseDeltaX_ = 0.0F;
    float mouseDeltaY_ = 0.0F;

    float scrollX_ = 0.0F;
    float scrollY_ = 0.0F;

    std::string text_;
  };

} // namespace yr::input