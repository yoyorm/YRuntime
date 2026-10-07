#include <catch2/catch_test_macros.hpp>

#include <yr/event/event_bus.h>
#include <yr/event/subscription.h>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

  using yr::evt::EventBus;
  using yr::evt::Subscription;

  struct DamageEvent {
    int amount = 0;
  };

  struct HealEvent {
    int amount = 0;
  };

  // 仅供 globalEventBus 测试使用的唯一事件类型，避免与其他用例共享全局分表。
  struct GlobalBusProbeEvent {
    int value = 0;
  };

  // 只按 const& 读取，因此事件类型本身可以是 move-only —— 验证类型擦除不要求可拷贝。
  struct MoveOnlyEvent {
    std::unique_ptr<int> payload;
  };

  // Subscription 必须是 move-only 的 RAII 句柄。
  static_assert(!std::is_copy_constructible_v<Subscription>);
  static_assert(!std::is_copy_assignable_v<Subscription>);
  static_assert(std::is_move_constructible_v<Subscription>);
  static_assert(std::is_move_assignable_v<Subscription>);

  // EventBus 保持公开可构造（局部实例合法），但禁止复制/移动。
  static_assert(std::is_default_constructible_v<EventBus>);
  static_assert(!std::is_copy_constructible_v<EventBus>);
  static_assert(!std::is_copy_assignable_v<EventBus>);
  static_assert(!std::is_move_constructible_v<EventBus>);
  static_assert(!std::is_move_assignable_v<EventBus>);

} // namespace

TEST_CASE("event bus: 默认构造的 Subscription 无效且 reset 安全", "[event][event_bus]") {
  Subscription sub;
  CHECK_FALSE(sub.valid());

  sub.reset(); // 空句柄不应崩溃
  CHECK_FALSE(sub.valid());
}

TEST_CASE("event bus: subscribe 后 publish 能收到事件", "[event][event_bus]") {
  EventBus bus;
  int received = 0;

  const Subscription sub =
      bus.subscribe<DamageEvent>([&received](const DamageEvent& event) { received = event.amount; });
  REQUIRE(sub.valid());

  bus.publish(DamageEvent{42});
  CHECK(received == 42);
}

TEST_CASE("event bus: 多个同类型订阅按注册顺序收到", "[event][event_bus]") {
  EventBus bus;
  std::vector<int> order;

  const Subscription first = bus.subscribe<DamageEvent>([&order](const DamageEvent&) { order.push_back(1); });
  const Subscription second = bus.subscribe<DamageEvent>([&order](const DamageEvent&) { order.push_back(2); });

  bus.publish(DamageEvent{});

  REQUIRE(order.size() == 2);
  CHECK(order[0] == 1);
  CHECK(order[1] == 2);
}

TEST_CASE("event bus: 不同类型的事件分表隔离", "[event][event_bus]") {
  EventBus bus;
  int damageCount = 0;
  int healCount = 0;

  const Subscription damage = bus.subscribe<DamageEvent>([&damageCount](const DamageEvent&) { ++damageCount; });
  const Subscription heal = bus.subscribe<HealEvent>([&healCount](const HealEvent&) { ++healCount; });

  bus.publish(DamageEvent{});
  CHECK(damageCount == 1);
  CHECK(healCount == 0);

  bus.publish(HealEvent{});
  CHECK(damageCount == 1);
  CHECK(healCount == 1);
}

TEST_CASE("event bus: Subscription::reset 后不再收到事件", "[event][event_bus]") {
  EventBus bus;
  int count = 0;

  Subscription sub = bus.subscribe<DamageEvent>([&count](const DamageEvent&) { ++count; });
  bus.publish(DamageEvent{});
  CHECK(count == 1);

  sub.reset();
  CHECK_FALSE(sub.valid());

  bus.publish(DamageEvent{});
  CHECK(count == 1);
}

TEST_CASE("event bus: Subscription 析构后不再收到事件（RAII）", "[event][event_bus]") {
  EventBus bus;
  int count = 0;

  {
    const Subscription sub = bus.subscribe<DamageEvent>([&count](const DamageEvent&) { ++count; });
    bus.publish(DamageEvent{});
    CHECK(count == 1);
  } // sub 析构 → 自动退订

  bus.publish(DamageEvent{});
  CHECK(count == 1);
}

TEST_CASE("event bus: unsubscribe 标记 inactive 后不再调用，且可重复调用", "[event][event_bus]") {
  EventBus bus;
  int count = 0;

  Subscription sub = bus.subscribe<DamageEvent>([&count](const DamageEvent&) { ++count; });
  bus.publish(DamageEvent{});
  CHECK(count == 1);

  bus.unsubscribe(sub);
  CHECK_FALSE(sub.valid());

  bus.unsubscribe(sub); // 幂等：重复退订不应崩溃
  bus.publish(DamageEvent{});
  CHECK(count == 1);
}

TEST_CASE("event bus: 取消其中一个订阅不影响其他订阅", "[event][event_bus]") {
  EventBus bus;
  int firstCount = 0;
  int secondCount = 0;

  Subscription first = bus.subscribe<DamageEvent>([&firstCount](const DamageEvent&) { ++firstCount; });
  const Subscription second = bus.subscribe<DamageEvent>([&secondCount](const DamageEvent&) { ++secondCount; });

  bus.unsubscribe(first);
  bus.publish(DamageEvent{});

  CHECK(firstCount == 0);
  CHECK(secondCount == 1);
}

TEST_CASE("event bus: move 构造转移订阅所有权", "[event][event_bus]") {
  EventBus bus;
  int count = 0;

  Subscription original = bus.subscribe<DamageEvent>([&count](const DamageEvent&) { ++count; });
  REQUIRE(original.valid());

  Subscription moved = std::move(original);
  CHECK_FALSE(original.valid());
  CHECK(moved.valid());

  bus.publish(DamageEvent{});
  CHECK(count == 1);

  moved.reset();
  bus.publish(DamageEvent{});
  CHECK(count == 1);
}

TEST_CASE("event bus: move 赋值释放原有绑定并接管新订阅", "[event][event_bus]") {
  EventBus bus;
  int damageCount = 0;
  int healCount = 0;

  Subscription damage = bus.subscribe<DamageEvent>([&damageCount](const DamageEvent&) { ++damageCount; });
  Subscription heal = bus.subscribe<HealEvent>([&healCount](const HealEvent&) { ++healCount; });

  bus.publish(DamageEvent{});
  bus.publish(HealEvent{});
  CHECK(damageCount == 1);
  CHECK(healCount == 1);

  heal = std::move(damage); // 覆盖 heal：原 Heal 订阅应被退订
  CHECK_FALSE(damage.valid());
  CHECK(heal.valid());

  bus.publish(DamageEvent{});
  bus.publish(HealEvent{});
  CHECK(damageCount == 2);
  CHECK(healCount == 1);
}

TEST_CASE("event bus: 没有订阅者时 publish 为 no-op", "[event][event_bus]") {
  const EventBus bus;
  bus.publish(DamageEvent{1});
  bus.publish(HealEvent{2});
  CHECK(true);
}

TEST_CASE("event bus: 事件类型可以是 move-only（类型擦除只按 const& 读取）", "[event][event_bus]") {
  EventBus bus;
  int observed = 0;

  const Subscription sub =
      bus.subscribe<MoveOnlyEvent>([&observed](const MoveOnlyEvent& event) { observed = *event.payload; });

  MoveOnlyEvent event{std::make_unique<int>(7)};
  bus.publish(event);
  CHECK(observed == 7);
}

TEST_CASE("event bus: globalEventBus 多次调用返回同一进程级实例", "[event][event_bus][global]") {
  const EventBus& first = yr::evt::globalEventBus();
  const EventBus& second = yr::evt::globalEventBus();
  CHECK(&first == &second);
}

TEST_CASE("event bus: 可通过 globalEventBus 订阅/发布/RAII 退订", "[event][event_bus][global]") {
  int received = 0;

  {
    const Subscription sub = yr::evt::globalEventBus().subscribe<GlobalBusProbeEvent>(
        [&received](const GlobalBusProbeEvent& event) { received = event.value; });
    REQUIRE(sub.valid());

    yr::evt::globalEventBus().publish(GlobalBusProbeEvent{99});
    CHECK(received == 99);
  } // sub 析构 → 从全局总线自动退订

  received = 0;
  yr::evt::globalEventBus().publish(GlobalBusProbeEvent{7});
  CHECK(received == 0);
}