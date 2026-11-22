#include <catch2/catch_test_macros.hpp>

#include <yr/core/string_interner.h>
#include <yr/input/input_state.h>

namespace {

  using yr::input::InputState;
  using yr::input::KeyEvent;
  using yr::input::KeyPhase;
  using yr::input::MouseButtonEvent;
  using yr::input::MouseMoveEvent;
  using yr::input::MouseWheelEvent;
  using yr::input::TextInputEvent;

  [[nodiscard]] yr::core::StringId intern(const char* text) {
    return yr::core::globalStringInterner().intern(text);
  }

  [[nodiscard]] KeyEvent keyEvent(const char* key, KeyPhase phase) {
    return KeyEvent{intern(key), phase, phase == KeyPhase::kReleased ? 0.0F : 1.0F};
  }

} // namespace

TEST_CASE("input state: key pressed, held and released across frames", "[input][state]") {
  InputState state;

  state.submit(keyEvent("key_w", KeyPhase::kPressed));
  CHECK(state.held(intern("key_w")));
  CHECK(state.pressed(intern("key_w")));

  state.beginFrame();
  CHECK(state.held(intern("key_w")));
  CHECK_FALSE(state.pressed(intern("key_w")));

  state.submit(keyEvent("key_w", KeyPhase::kReleased));
  CHECK_FALSE(state.held(intern("key_w")));
  CHECK(state.released(intern("key_w")));
}

TEST_CASE("input state: press and release in the same frame keep both edges", "[input][state]") {
  InputState state;

  state.submit(keyEvent("key_space", KeyPhase::kPressed));
  state.submit(keyEvent("key_space", KeyPhase::kReleased));

  CHECK_FALSE(state.held(intern("key_space")));
  CHECK(state.pressed(intern("key_space")));
  CHECK(state.released(intern("key_space")));
}

TEST_CASE("input state: repeat does not change held but is observable", "[input][state]") {
  InputState state;

  state.submit(keyEvent("key_a", KeyPhase::kPressed));
  state.submit(keyEvent("key_a", KeyPhase::kRepeat));

  CHECK(state.held(intern("key_a")));
  CHECK(state.repeated(intern("key_a")));
  CHECK_FALSE(state.released(intern("key_a")));
}

TEST_CASE("input state: mouse buttons, cursor, wheel and text are summarized per frame", "[input][state]") {
  InputState state;

  state.submit(MouseButtonEvent{
      .button = MouseButtonEvent::Button::kLeft,
      .phase = KeyPhase::kPressed,
      .x = 10.0F,
      .y = 20.0F,
  });
  state.submit(MouseMoveEvent{.x = 15.0F, .y = 25.0F, .deltaX = 5.0F, .deltaY = 5.0F});
  state.submit(MouseWheelEvent{.deltaX = 1.5F, .deltaY = -2.0F});
  state.submit(TextInputEvent{.text = "ab"});
  state.submit(TextInputEvent{.text = "c"});

  CHECK(state.held(intern("mouse_left")));
  CHECK(state.pressed(intern("mouse_left")));
  CHECK(state.mouseX() == 15.0F);
  CHECK(state.mouseY() == 25.0F);
  CHECK(state.mouseDeltaX() == 5.0F);
  CHECK(state.mouseDeltaY() == 5.0F);
  CHECK(state.scrollX() == 1.5F);
  CHECK(state.scrollY() == -2.0F);
  CHECK(state.text() == "abc");

  state.beginFrame();
  CHECK(state.held(intern("mouse_left")));
  CHECK_FALSE(state.pressed(intern("mouse_left")));
  CHECK(state.mouseX() == 15.0F);
  CHECK(state.mouseDeltaX() == 0.0F);
  CHECK(state.scrollX() == 0.0F);
  CHECK(state.text().empty());

  state.submit(MouseButtonEvent{
      .button = MouseButtonEvent::Button::kLeft,
      .phase = KeyPhase::kReleased,
      .x = 15.0F,
      .y = 25.0F,
  });
  CHECK_FALSE(state.held(intern("mouse_left")));
  CHECK(state.released(intern("mouse_left")));
}