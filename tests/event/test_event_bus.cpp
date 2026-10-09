#include <catch2/catch_test_macros.hpp>

#include <yr/core/assert.h>
#include <yr/event/event_bus.h>
#include <yr/event/subscription.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
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

  // 确定性重放用的两类事件：跨类型 post + flush 派发中再次 post。
  struct ReplayAEvent {
    int value = 0;
  };

  struct ReplayBEvent {
    int value = 0;
  };

  // 构造固定输入并返回完整回调日志。每次调用都新建局部 EventBus，不依赖 globalEventBus 残留状态。
  std::vector<std::string> runDeterministicReplayScenario() {
    EventBus bus;
    std::vector<std::string> log;

    const Subscription a = bus.subscribe<ReplayAEvent>([&bus, &log](const ReplayAEvent& event) {
      log.push_back("A" + std::to_string(event.value));
      if (event.value == 1) {
        bus.post(ReplayBEvent{10}); // 跨类型 post：进入下一缓冲，不在本轮派发
      }
    });
    const Subscription b = bus.subscribe<ReplayBEvent>([&bus, &log](const ReplayBEvent& event) {
      log.push_back("B" + std::to_string(event.value));
      if (event.value == 10) {
        bus.post(ReplayAEvent{2}); // flush 派发中再次 post：进入下一轮 flush
      }
    });

    bus.post(ReplayAEvent{1});
    bus.post(ReplayBEvent{20});
    bus.flush(); // 派发 A1、B20；A1 回调再 post B10
    bus.flush(); // 派发 B10；B10 回调再 post A2
    bus.flush(); // 派发 A2

    return log;
  }

  // EventBus 保持公开可构造（局部实例合法），但禁止复制/移动。
  static_assert(std::is_default_constructible_v<EventBus>);
  static_assert(!std::is_copy_constructible_v<EventBus>);
  static_assert(!std::is_copy_assignable_v<EventBus>);
  static_assert(!std::is_move_constructible_v<EventBus>);
  static_assert(!std::is_move_assignable_v<EventBus>);

  // 递归 flush 测试需要让断言"报告后继续"，否则默认 handler 会直接 trap。
#if YR_ENABLE_ASSERTS
  int g_flushAssertCount = 0;
  yr::core::AssertAction flushCountingHandler(const yr::core::AssertInfo&) noexcept {
    ++g_flushAssertCount;
    return yr::core::AssertAction::kContinue;
  }
#endif

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
  // publish 非 const（深度计数/清理会修改 EventBus），因此这里用非 const 实例。
  EventBus bus;
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

TEST_CASE("event bus: post 不立即发布，flush 才按入队顺序派发", "[event][event_bus][post_flush]") {
  EventBus bus;
  std::vector<int> received;
  const Subscription sub =
      bus.subscribe<DamageEvent>([&received](const DamageEvent& event) { received.push_back(event.amount); });

  bus.post(DamageEvent{11}); // 右值：走 move 路径
  DamageEvent second{22};
  bus.post(second); // 左值：走 copy 路径

  CHECK(received.empty()); // post 不立即发布
  CHECK(bus.pending() == 2);

  bus.flush();

  REQUIRE(received.size() == 2);
  CHECK(received[0] == 11);
  CHECK(received[1] == 22);
  CHECK(bus.pending() == 0);
}

TEST_CASE("event bus: flush 按跨事件类型的 post 顺序派发", "[event][event_bus][post_flush]") {
  EventBus bus;
  std::vector<int> order;
  const Subscription damage =
      bus.subscribe<DamageEvent>([&order](const DamageEvent& event) { order.push_back(event.amount); });
  const Subscription heal =
      bus.subscribe<HealEvent>([&order](const HealEvent& event) { order.push_back(100 + event.amount); });

  bus.post(DamageEvent{1});
  bus.post(HealEvent{2});
  bus.post(DamageEvent{3});
  bus.post(HealEvent{4});
  CHECK(order.empty());

  bus.flush();

  REQUIRE(order.size() == 4);
  CHECK(order[0] == 1);
  CHECK(order[1] == 102);
  CHECK(order[2] == 3);
  CHECK(order[3] == 104);
}

TEST_CASE("event bus: flush 期间 post 的事件留到下一次 flush", "[event][event_bus][post_flush]") {
  EventBus bus;
  std::vector<int> received;
  const Subscription sub = bus.subscribe<DamageEvent>([&bus, &received](const DamageEvent& event) {
    received.push_back(event.amount);
    if (event.amount == 1) {
      bus.post(DamageEvent{2}); // 必须进入另一缓冲，不在本轮派发
    }
  });

  bus.post(DamageEvent{1});
  bus.flush();

  REQUIRE(received.size() == 1);
  CHECK(received[0] == 1);

  // 本次 flush 中 post 的事件等待下一次 flush。
  CHECK(bus.pending() == 1);

  bus.flush();

  REQUIRE(received.size() == 2);
  CHECK(received[1] == 2);
  CHECK(bus.pending() == 0);
}

TEST_CASE("event bus: 空队列 flush 安全", "[event][event_bus][post_flush]") {
  EventBus bus;

  bus.flush();
  bus.flush();

  CHECK(bus.pending() == 0);
}

TEST_CASE("event bus: 递归 flush 直接返回且不处理下一队列", "[event][event_bus][post_flush][assert]") {
  EventBus bus;
  std::vector<int> received;
  bool reentered = false;
  const Subscription sub = bus.subscribe<DamageEvent>([&](const DamageEvent& event) {
    received.push_back(event.amount);
    if (!reentered) {
      reentered = true;
      bus.post(DamageEvent{99}); // 进入下一队列
      bus.flush();               // 递归 flush：应为无副作用的 no-op
    }
  });

  bus.post(DamageEvent{1});

#if YR_ENABLE_ASSERTS
  g_flushAssertCount = 0;
  yr::core::setAssertHandler(flushCountingHandler);
#endif
  bus.flush();
#if YR_ENABLE_ASSERTS
  yr::core::setAssertHandler(nullptr);
  CHECK(g_flushAssertCount == 1); // 触发断言但 handler 允许继续
#endif

  REQUIRE(received.size() == 1); // 递归 flush 没有提前派发 99
  CHECK(received[0] == 1);
  CHECK(bus.pending() == 1);

  bus.flush();

  REQUIRE(received.size() == 2);
  CHECK(received[1] == 99);
  CHECK(bus.pending() == 0);
}

TEST_CASE("event bus: move-only 事件可以 post/flush", "[event][event_bus][post_flush]") {
  EventBus bus;
  int observed = 0;
  const Subscription sub =
      bus.subscribe<MoveOnlyEvent>([&observed](const MoveOnlyEvent& event) { observed = *event.payload; });

  bus.post(MoveOnlyEvent{std::make_unique<int>(7)});
  CHECK(observed == 0);
  CHECK(bus.pending() == 1);

  bus.flush();

  CHECK(observed == 7);
  CHECK(bus.pending() == 0);
}

TEST_CASE("event bus: 连续多次 flush 正确切换队列，不重复不丢事件", "[event][event_bus][post_flush]") {
  EventBus bus;
  std::vector<int> received;
  const Subscription sub = bus.subscribe<DamageEvent>([&bus, &received](const DamageEvent& event) {
    received.push_back(event.amount);
    if (event.amount % 2 == 1) {
      bus.post(DamageEvent{event.amount + 1}); // 每次奇数事件派生一个偶数事件到下一轮
    }
  });

  bus.post(DamageEvent{1});
  bus.flush();
  REQUIRE(received == std::vector<int>{1});
  CHECK(bus.pending() == 1);

  bus.flush();
  REQUIRE(received == std::vector<int>{1, 2}); // 2 为偶数，不再派生
  CHECK(bus.pending() == 0);

  bus.post(DamageEvent{3});
  bus.flush();
  REQUIRE(received == std::vector<int>{1, 2, 3});
  CHECK(bus.pending() == 1);

  bus.flush();
  REQUIRE(received == std::vector<int>{1, 2, 3, 4});
  CHECK(bus.pending() == 0);

  bus.flush(); // 已空，再次 flush 不应重复
  REQUIRE(received == std::vector<int>{1, 2, 3, 4});
}

TEST_CASE("event bus: 确定性重放——固定输入两次运行的完整回调日志逐项相同", "[event][event_bus][determinism]") {
  const std::vector<std::string> first = runDeterministicReplayScenario();
  const std::vector<std::string> second = runDeterministicReplayScenario();

  CHECK(first == second); // 同输入 → 同顺序，逐项相同
  CHECK(first == std::vector<std::string>{"A1", "B20", "B10", "A2"});
}

TEST_CASE("event bus: 1000 个有序事件 flush 后数量正确、保序、不重复不丢失", "[event][event_bus][stress]") {
  EventBus bus;
  std::vector<int> received;
  const Subscription sub =
      bus.subscribe<DamageEvent>([&received](const DamageEvent& event) { received.push_back(event.amount); });

  constexpr int kEventCount = 1000;
  for (int i = 0; i < kEventCount; ++i) {
    bus.post(DamageEvent{i});
  }
  REQUIRE(bus.pending() == static_cast<std::size_t>(kEventCount));

  bus.flush();

  REQUIRE(received.size() == static_cast<std::size_t>(kEventCount)); // 数量正确、无丢失
  for (int i = 0; i < kEventCount; ++i) {
    CHECK(received[static_cast<std::size_t>(i)] == i); // 严格保序
  }

  std::vector<int> sorted = received;
  std::sort(sorted.begin(), sorted.end());
  CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end()); // 无重复
  CHECK(bus.pending() == 0);                                               // 无残留
}