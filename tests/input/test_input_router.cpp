#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <yr/core/string_interner.h>
#include <yr/input/input_router.h>

namespace {

  using yr::input::InputEvent;
  using yr::input::InputRouter;
  using yr::input::KeyEvent;
  using yr::input::KeyPhase;
  using yr::input::TextInputEvent;

  [[nodiscard]] yr::core::StringId intern(const char* text) {
    return yr::core::globalStringInterner().intern(text);
  }

  [[nodiscard]] InputEvent keyEvent(const char* key, KeyPhase phase, float value = 1.0F) {
    return KeyEvent{intern(key), phase, value};
  }

  // 每帧把 pending 里的内容原样交给 Router；测试用它验证 Router 的拼接与清理。
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

TEST_CASE("input router: concatenates sources in registration order", "[input][router]") {
  InputRouter router;

  auto first = std::make_unique<FakeSource>();
  auto second = std::make_unique<FakeSource>();
  first->pending.push_back(keyEvent("key_w", KeyPhase::kPressed));
  first->pending.push_back(keyEvent("key_w", KeyPhase::kReleased));
  second->pending.push_back(keyEvent("key_a", KeyPhase::kPressed));

  FakeSource* const firstRaw = first.get();
  FakeSource* const secondRaw = second.get();
  router.addSource(std::move(first));
  router.addSource(std::move(second));

  router.beginFrame();

  const std::span<const InputEvent> events = router.events();
  REQUIRE(events.size() == 3);
  const auto* firstKey = std::get_if<KeyEvent>(&events[0]);
  const auto* secondKey = std::get_if<KeyEvent>(&events[1]);
  const auto* thirdKey = std::get_if<KeyEvent>(&events[2]);
  REQUIRE(firstKey != nullptr);
  REQUIRE(secondKey != nullptr);
  REQUIRE(thirdKey != nullptr);
  CHECK(firstKey->key.view() == "key_w");
  CHECK(firstKey->phase == KeyPhase::kPressed);
  CHECK(secondKey->phase == KeyPhase::kReleased);
  CHECK(thirdKey->key.view() == "key_a");
  CHECK(firstRaw->pending.empty());
  CHECK(secondRaw->pending.empty());
}

TEST_CASE("input router: beginFrame clears the previous frame before polling", "[input][router]") {
  InputRouter router;
  auto source = std::make_unique<FakeSource>();
  FakeSource* const raw = source.get();
  router.addSource(std::move(source));

  raw->pending.push_back(keyEvent("key_w", KeyPhase::kPressed));
  router.beginFrame();
  REQUIRE(router.events().size() == 1);

  router.beginFrame();
  CHECK(router.events().empty());

  raw->pending.push_back(keyEvent("key_w", KeyPhase::kReleased));
  router.beginFrame();
  REQUIRE(router.events().size() == 1);
  const auto* event = std::get_if<KeyEvent>(&router.events()[0]);
  REQUIRE(event != nullptr);
  CHECK(event->phase == KeyPhase::kReleased);
}

TEST_CASE("input router: forwards non-key events without losing payload", "[input][router]") {
  InputRouter router;
  auto source = std::make_unique<FakeSource>();
  FakeSource* const raw = source.get();
  router.addSource(std::move(source));

  raw->pending.push_back(TextInputEvent{std::string(512, 'x')});
  router.beginFrame();

  REQUIRE(router.events().size() == 1);
  const auto* text = std::get_if<TextInputEvent>(&router.events()[0]);
  REQUIRE(text != nullptr);
  CHECK(text->text.size() == 512);
  CHECK(text->text.front() == 'x');
  CHECK(text->text.back() == 'x');
}

TEST_CASE("input router: empty router produces an empty frame", "[input][router]") {
  InputRouter router;

  router.beginFrame();

  CHECK(router.events().empty());
}
