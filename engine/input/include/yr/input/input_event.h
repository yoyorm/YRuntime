#pragma once
#include "yr/core/string_id.h"
#include <string>
#include <variant>

namespace yr::input {
  using KeyCode = yr::core::StringId;
  enum class KeyPhase { kPressed, kHeld, kReleased, kRepeat, kTextInput };

  struct KeyEvent {
    KeyCode key;
    KeyPhase phase;
    float value = 0.0f;
  };
  struct MouseButtonEvent {
    enum class Button { kLeft, kRight, kMiddle, kOther };
    Button button;
    KeyPhase phase;
    float x = 0.0f;
    float y = 0.0f;
  };
  struct MouseMoveEvent {
    float x = 0.0f;
    float y = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
  };
  struct MouseWheelEvent {
    float deltaX = 0.0f;
    float deltaY = 0.0f;
  };
  struct TextInputEvent {
    std::string text;
  };
  using InputEvent = std::variant<KeyEvent, MouseButtonEvent, MouseMoveEvent, MouseWheelEvent, TextInputEvent>;

} // namespace yr::input
