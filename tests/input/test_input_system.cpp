#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>
#include <vector>

#include <yr/core/string_interner.h>
#include <yr/event/event_bus.h>
#include <yr/input/input_system.h>

namespace {

  using yr::input::InputEvent;
  using yr::input::InputSystem;
  using yr::input::KeyEvent;
  using yr::input::KeyPhase;

  [[nodiscard]] yr::core::StringId intern(const char* text) {
    return yr::core::globalStringInterner().intern(text);
  }

  class FakeSource final : public yr::input::IInputSource {
  public:
    void poll(yr::input::IInputSink& sink) override {
      for (InputEvent& event : pending) {
        sink.submit(std::move(event));
      }
      pending.clear();
    }

    std::vector<InputEvent> pending;
  };

} // namespace

TEST_CASE("input system: updates state and posts raw events to the bus", "[input][system]") {
  yr::evt::EventBus bus;
  InputSystem system;
  system.setEventBus(&bus);

  auto source = std::make_unique<FakeSource>();
  FakeSource* const raw = source.get();
  system.addSource(std::move(source));

  int received = 0;
  const yr::evt::Subscription subscription = bus.subscribe<InputEvent>([&received](const InputEvent& event) {
    if (const auto* key = std::get_if<KeyEvent>(&event); key != nullptr && key->key == intern("key_w")) {
      ++received;
    }
  });

  raw->pending.push_back(KeyEvent{intern("key_w"), KeyPhase::kPressed, 1.0F});
  system.beginFrame();

  CHECK(system.state().held(intern("key_w")));
  CHECK(system.state().pressed(intern("key_w")));
  CHECK(bus.pending() == 1);

  bus.flush();
  CHECK(received == 1);

  system.beginFrame();
  CHECK(system.state().held(intern("key_w")));
  CHECK_FALSE(system.state().pressed(intern("key_w")));
  CHECK(bus.pending() == 0);
}

TEST_CASE("input system: works without an event bus", "[input][system]") {
  InputSystem system;

  auto source = std::make_unique<FakeSource>();
  FakeSource* const raw = source.get();
  system.addSource(std::move(source));

  raw->pending.push_back(KeyEvent{intern("key_a"), KeyPhase::kPressed, 1.0F});
  system.beginFrame();

  CHECK(system.state().held(intern("key_a")));
  CHECK(system.state().pressed(intern("key_a")));
}