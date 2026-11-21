#include "glfw_input_source.h"

#include <cstdint>
#include <string>
#include <utility>

#include <yr/core/log.h>
#include <yr/core/string_interner.h>

namespace yr::demo {

  namespace {

    [[nodiscard]] yr::core::StringId keyCode(yr::render::Key key) {
      yr::core::StringInterner& interner = yr::core::globalStringInterner();
      switch (key) {
      case yr::render::Key::W:
        return interner.intern("key_w");
      case yr::render::Key::A:
        return interner.intern("key_a");
      case yr::render::Key::S:
        return interner.intern("key_s");
      case yr::render::Key::D:
        return interner.intern("key_d");
      case yr::render::Key::Left:
        return interner.intern("key_left");
      case yr::render::Key::Right:
        return interner.intern("key_right");
      case yr::render::Key::Up:
        return interner.intern("key_up");
      case yr::render::Key::Down:
        return interner.intern("key_down");
      case yr::render::Key::Space:
        return interner.intern("key_space");
      case yr::render::Key::Escape:
        return interner.intern("key_escape");
      case yr::render::Key::Enter:
        return interner.intern("key_enter");
      case yr::render::Key::Unknown:
        return yr::core::StringId::invalid();
      }
      return yr::core::StringId::invalid();
    }

    [[nodiscard]] yr::input::KeyPhase toKeyPhase(yr::render::KeyAction action) noexcept {
      switch (action) {
      case yr::render::KeyAction::kRelease:
        return yr::input::KeyPhase::kReleased;
      case yr::render::KeyAction::kRepeat:
        return yr::input::KeyPhase::kRepeat;
      case yr::render::KeyAction::kPress:
      default:
        return yr::input::KeyPhase::kPressed;
      }
    }

    [[nodiscard]] yr::input::MouseButtonEvent::Button toInputButton(yr::render::MouseButton button) noexcept {
      switch (button) {
      case yr::render::MouseButton::kLeft:
        return yr::input::MouseButtonEvent::Button::kLeft;
      case yr::render::MouseButton::kRight:
        return yr::input::MouseButtonEvent::Button::kRight;
      case yr::render::MouseButton::kMiddle:
        return yr::input::MouseButtonEvent::Button::kMiddle;
      case yr::render::MouseButton::kOther:
      default:
        return yr::input::MouseButtonEvent::Button::kOther;
      }
    }

    [[nodiscard]] std::string encodeUtf8(char32_t codepoint) {
      const std::uint32_t cp = static_cast<std::uint32_t>(codepoint);
      if (cp > 0x10FFFFU) {
        return "?";
      }

      std::string out;
      if (cp <= 0x7FU) {
        out.push_back(static_cast<char>(cp));
      } else if (cp <= 0x7FFU) {
        out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
      } else if (cp <= 0xFFFFU) {
        out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
      } else {
        out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
      }
      return out;
    }

  } // namespace

  GlfwInputSource::GlfwInputSource(yr::render::VulkanBackend& backend) : backend_(backend) {
    backend_.setInputCallbacks(yr::render::InputCallbacks{
        .key = [this](yr::render::Key key, yr::render::KeyAction action,
                      yr::render::KeyMods /*mods*/) { onKey(key, action); },
        .mouseButton = [this](yr::render::MouseButton button, yr::render::KeyAction action,
                              yr::render::KeyMods /*mods*/, double x,
                              double y) { onMouseButton(button, action, x, y); },
        .cursorPos = [this](double x, double y) { onCursor(x, y); },
        .scroll = [this](double dx, double dy) { onScroll(dx, dy); },
        .character = [this](char32_t codepoint) { onCharacter(codepoint); },
    });
  }

  GlfwInputSource::~GlfwInputSource() {
    backend_.setInputCallbacks({});
  }

  void GlfwInputSource::poll(yr::input::IInputSink& sink) {
    backend_.pollEvents();
    for (yr::input::InputEvent& event : pending_) {
      sink.submit(std::move(event));
    }
    pending_.clear();
  }

  void GlfwInputSource::onKey(yr::render::Key key, yr::render::KeyAction action) {
    const yr::core::StringId code = keyCode(key);
    if (!code.valid()) {
      return;
    }

    const yr::input::KeyPhase phase = toKeyPhase(action);
    pending_.push_back(yr::input::KeyEvent{
        .key = code,
        .phase = phase,
        .value = phase == yr::input::KeyPhase::kReleased ? 0.0F : 1.0F,
    });
    YR_LOG_DEBUG("input.glfw", "key " + std::string(code.view()) +
                                   (phase == yr::input::KeyPhase::kReleased ? " released" : " pressed"));
  }

  void GlfwInputSource::onMouseButton(yr::render::MouseButton button, yr::render::KeyAction action, double x,
                                      double y) {
    pending_.push_back(yr::input::MouseButtonEvent{
        .button = toInputButton(button),
        .phase = toKeyPhase(action),
        .x = static_cast<float>(x),
        .y = static_cast<float>(y),
    });
    YR_LOG_DEBUG("input.glfw", "mouse button " + std::to_string(static_cast<int>(button)) +
                                   (action == yr::render::KeyAction::kRelease ? " released" : " pressed"));
  }

  void GlfwInputSource::onCursor(double x, double y) {
    const float deltaX = hasCursor_ ? static_cast<float>(x - lastX_) : 0.0F;
    const float deltaY = hasCursor_ ? static_cast<float>(y - lastY_) : 0.0F;
    lastX_ = x;
    lastY_ = y;
    hasCursor_ = true;
    pending_.push_back(yr::input::MouseMoveEvent{
        .x = static_cast<float>(x),
        .y = static_cast<float>(y),
        .deltaX = deltaX,
        .deltaY = deltaY,
    });
  }

  void GlfwInputSource::onScroll(double dx, double dy) {
    pending_.push_back(yr::input::MouseWheelEvent{
        .deltaX = static_cast<float>(dx),
        .deltaY = static_cast<float>(dy),
    });
  }

  void GlfwInputSource::onCharacter(char32_t codepoint) {
    std::string text = encodeUtf8(codepoint);
    pending_.push_back(yr::input::TextInputEvent{.text = text});
    YR_LOG_DEBUG("input.glfw", "text " + text);
  }

} // namespace yr::demo