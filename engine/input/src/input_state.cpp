#include "yr/input/input_state.h"

#include <variant>

#include <yr/core/string_interner.h>

namespace yr::input {

  namespace {

    [[nodiscard]] KeyCode mouseCode(MouseButtonEvent::Button button) {
      yr::core::StringInterner& interner = yr::core::globalStringInterner();
      switch (button) {
      case MouseButtonEvent::Button::kLeft:
        return interner.intern("mouse_left");
      case MouseButtonEvent::Button::kRight:
        return interner.intern("mouse_right");
      case MouseButtonEvent::Button::kMiddle:
        return interner.intern("mouse_middle");
      case MouseButtonEvent::Button::kOther:
        return interner.intern("mouse_other");
      }
      return interner.intern("mouse_other");
    }

  } // namespace

  void InputState::beginFrame() {
    pressed_.clear();
    released_.clear();
    repeated_.clear();

    mouseDeltaX_ = 0.0F;
    mouseDeltaY_ = 0.0F;
    scrollX_ = 0.0F;
    scrollY_ = 0.0F;
    text_.clear();

    // 保留 held_、mouseX_、mouseY_。
  }

  void InputState::submit(InputEvent event) {
    if (auto* key = std::get_if<KeyEvent>(&event)) {
      applyButton(key->key, key->phase);
      return;
    }
    if (auto* mouse = std::get_if<MouseButtonEvent>(&event)) {
      applyButton(mouseCode(mouse->button), mouse->phase);
      mouseX_ = mouse->x;
      mouseY_ = mouse->y;
      return;
    }
    if (auto* move = std::get_if<MouseMoveEvent>(&event)) {
      mouseX_ = move->x;
      mouseY_ = move->y;
      mouseDeltaX_ += move->deltaX;
      mouseDeltaY_ += move->deltaY;
      return;
    }
    if (auto* wheel = std::get_if<MouseWheelEvent>(&event)) {
      scrollX_ += wheel->deltaX;
      scrollY_ += wheel->deltaY;
      return;
    }
    if (auto* text = std::get_if<TextInputEvent>(&event)) {
      text_ += text->text;
    }
  }

  bool InputState::held(KeyCode code) const noexcept {
    return held_.contains(code);
  }

  bool InputState::pressed(KeyCode code) const noexcept {
    return pressed_.contains(code);
  }

  bool InputState::released(KeyCode code) const noexcept {
    return released_.contains(code);
  }

  bool InputState::repeated(KeyCode code) const noexcept {
    return repeated_.contains(code);
  }

  void InputState::applyButton(KeyCode code, KeyPhase phase) {
    switch (phase) {
    case KeyPhase::kPressed:
      held_.insert(code);
      pressed_.insert(code);
      break;
    case KeyPhase::kReleased:
      held_.erase(code);
      released_.insert(code);
      break;
    case KeyPhase::kRepeat:
      repeated_.insert(code);
      break;
    case KeyPhase::kHeld:
      held_.insert(code);
      break;
    case KeyPhase::kTextInput:
      break;
    }
  }

} // namespace yr::input