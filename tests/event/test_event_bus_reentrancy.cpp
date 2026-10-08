#include <catch2/catch_test_macros.hpp>

#include <yr/core/assert.h>
#include <yr/event/event_bus.h>
#include <yr/event/subscription.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

  using yr::evt::EventBus;
  using yr::evt::Subscription;

  struct SelfUnsubEvent {
    int value = 0;
  };

  struct AbEvent {};

  struct GrowthEvent {
    int value = 0;
  };

  struct OuterEvent {};

  struct NestEvent {
    int value = 0;
  };

  struct CleanupEvent {};

  struct ThrowEvent {};

  // 递归深度测试：同类型与跨类型各一组事件。
  struct DepthEvent {};
  struct DepthAEvent {};
  struct DepthBEvent {};

  // 深度超限断言需要"报告后继续"，否则默认 handler 会直接 trap。
#if YR_ENABLE_ASSERTS
  int g_depthAssertCount = 0;
  yr::core::AssertAction depthCountingHandler(const yr::core::AssertInfo&) noexcept {
    ++g_depthAssertCount;
    return yr::core::AssertAction::kContinue;
  }
#endif

  // 一组互不相同的空事件类型：在一次 publish 回调中批量插入新 key，逼出 unordered_map rehash。
  template <int N> struct TaggedEvent {};

  template <std::size_t... I>
  void subscribeTaggedRange(EventBus& bus, std::vector<Subscription>& out, std::index_sequence<I...>) {
    (out.push_back(bus.subscribe<TaggedEvent<static_cast<int>(I)>>([](const TaggedEvent<static_cast<int>(I)>&) {})),
     ...);
  }

} // namespace

TEST_CASE("event bus（重入）: 回调中退订自己当前继续执行、后续不再调用", "[event][event_bus][reentrancy]") {
  EventBus bus;
  std::vector<int> calls;
  Subscription self;
  self = bus.subscribe<SelfUnsubEvent>([&self, &calls](const SelfUnsubEvent& event) {
    calls.push_back(event.value);
    self.reset();
  });

  bus.publish(SelfUnsubEvent{1}); // 当前回调执行完
  CHECK(calls == std::vector<int>{1});

  bus.publish(SelfUnsubEvent{2}); // 已退订，不再调用
  CHECK(calls == std::vector<int>{1});
}

TEST_CASE("event bus（重入）: 回调退订尚未执行的其他订阅者本轮即时生效", "[event][event_bus][reentrancy]") {
  EventBus bus;
  std::vector<int> order;
  Subscription second;
  const Subscription first = bus.subscribe<AbEvent>([&second, &order](const AbEvent&) {
    order.push_back(1);
    second.reset(); // second 尚未执行，本轮应跳过
  });
  second = bus.subscribe<AbEvent>([&order](const AbEvent&) { order.push_back(2); });

  bus.publish(AbEvent{});
  CHECK(order == std::vector<int>{1});
}

TEST_CASE("event bus（重入）: publish 中新增同类型订阅下轮生效且不因扩容失效", "[event][event_bus][reentrancy]") {
  EventBus bus;
  std::vector<int> order;
  std::vector<Subscription> added;

  const Subscription spawner = bus.subscribe<GrowthEvent>([&](const GrowthEvent& event) {
    order.push_back(event.value);
    if (event.value == 1) {
      // 数量远大于 deque 块大小；若分表是 vector，此处会多次扩容并移动正在执行的 std::function。
      for (int i = 0; i < 256; ++i) {
        added.push_back(bus.subscribe<GrowthEvent>([i, &order](const GrowthEvent&) { order.push_back(10000 + i); }));
      }
    }
  });

  bus.publish(GrowthEvent{1});
  REQUIRE(order == std::vector<int>{1}); // 本轮只调用旧订阅

  order.clear();
  bus.publish(GrowthEvent{2});
  REQUIRE(order.size() == 257);
  CHECK(order.front() == 2); // 旧订阅仍在，且顺序在前
  CHECK(order[1] == 10000);
  CHECK(order[256] == 10255);
}

TEST_CASE("event bus（重入）: publish 中新增其他事件类型触发 rehash 不影响当前遍历", "[event][event_bus][reentrancy]") {
  EventBus bus;
  std::vector<int> calls;
  std::vector<Subscription> extraTypes;
  bool populated = false;

  const Subscription first = bus.subscribe<OuterEvent>([&](const OuterEvent&) {
    calls.push_back(1);
    if (!populated) {
      populated = true;
      // 插入 64 个新 key：map 会 rehash，但分表引用仍有效。
      subscribeTaggedRange(bus, extraTypes, std::make_index_sequence<64>{});
    }
  });
  const Subscription second = bus.subscribe<OuterEvent>([&](const OuterEvent&) { calls.push_back(2); });

  bus.publish(OuterEvent{});
  CHECK(calls == std::vector<int>{1, 2}); // rehash 后仍遍历到第二个订阅者
  CHECK(extraTypes.size() == 64);

  // 回调中新增的事件类型订阅之后可用。
  int taggedHits = 0;
  const Subscription tagged = bus.subscribe<TaggedEvent<63>>([&taggedHits](const TaggedEvent<63>&) { ++taggedHits; });
  bus.publish(TaggedEvent<63>{});
  CHECK(taggedHits == 1);
}

TEST_CASE("event bus（重入）: 嵌套 publish 中的退订延迟到最外层清理", "[event][event_bus][reentrancy]") {
  EventBus bus;
  std::vector<int> calls;
  Subscription second;

  const Subscription first = bus.subscribe<NestEvent>([&](const NestEvent& event) {
    calls.push_back(1);
    if (event.value == 1) {
      bus.publish(NestEvent{2}); // 嵌套：内层返回时不得清理 inactive
    } else if (event.value == 2) {
      second.reset(); // 内层回调退订外层尚未执行到的 second
    }
  });
  second = bus.subscribe<NestEvent>([&](const NestEvent&) { calls.push_back(2); });

  // 外层 first(1) → 内层 first(2，退订 second)；内层 second 已 inactive 跳过；
  // 回到外层继续 second：仍 inactive 跳过。若内层返回时就 erase，外层索引会失效。
  bus.publish(NestEvent{1});
  CHECK(calls == std::vector<int>{1, 1});

  calls.clear();
  bus.publish(NestEvent{3}); // 最外层结束后 second 已被清理；first 仍有效
  CHECK(calls == std::vector<int>{1});
}

TEST_CASE("event bus（重入）: 最外层 publish 后 inactive Entry 被物理清理", "[event][event_bus][reentrancy]") {
  EventBus bus;
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  {
    Subscription sub = bus.subscribe<CleanupEvent>([token](const CleanupEvent&) {});
    sub.reset(); // 只标记 inactive，分表仍持有捕获 token 的 std::function
  }
  token.reset(); // 现在只剩分表里的闭包持有 token

  CHECK_FALSE(weak.expired());

  bus.publish(CleanupEvent{}); // 最外层 publish 返回时清理 inactive
  CHECK(weak.expired());
}

TEST_CASE("event bus（重入）: 回调抛异常后 RAII 恢复深度并仍执行清理", "[event][event_bus][reentrancy][exception]") {
  EventBus bus;
  bool shouldThrow = true;
  int afterCalls = 0;

  auto token = std::make_shared<int>(2);
  std::weak_ptr<int> weak = token;
  {
    Subscription victim = bus.subscribe<ThrowEvent>([token](const ThrowEvent&) {});
    victim.reset(); // inactive，分表仍持有 token
  }
  token.reset();
  CHECK_FALSE(weak.expired());

  const Subscription thrower = bus.subscribe<ThrowEvent>([&shouldThrow](const ThrowEvent&) {
    if (shouldThrow) {
      throw std::runtime_error("boom");
    }
  });
  const Subscription after = bus.subscribe<ThrowEvent>([&afterCalls](const ThrowEvent&) { ++afterCalls; });

  CHECK_THROWS_AS(bus.publish(ThrowEvent{}), std::runtime_error);
  CHECK(afterCalls == 0); // 异常中断本轮，后续订阅者未执行
  // guard 在异常展开时把 publishDepth_ 归零并清理 inactive。
  CHECK(weak.expired());

  shouldThrow = false;
  bus.publish(ThrowEvent{}); // 深度已恢复，后续 publish 正常
  CHECK(afterCalls == 1);
}

TEST_CASE("event bus（重入）: 同类型递归 publish 只执行到深度上限，第 9 层被拒绝",
          "[event][event_bus][reentrancy][depth][assert]") {
  EventBus bus;
  int callbackCount = 0;
  bool recurse = true;
  const Subscription sub = bus.subscribe<DepthEvent>([&](const DepthEvent&) {
    ++callbackCount;
    if (recurse) {
      bus.publish(DepthEvent{});
    }
  });

#if YR_ENABLE_ASSERTS
  g_depthAssertCount = 0;
  yr::core::setAssertHandler(depthCountingHandler);
#endif
  bus.publish(DepthEvent{});
#if YR_ENABLE_ASSERTS
  yr::core::setAssertHandler(nullptr);
  CHECK(g_depthAssertCount == 1); // 第 9 层恰好报告一次
#endif

  // 第 1..8 层各执行一次回调；第 9 层被拒绝（Release 断言被编译掉，但同样停在 8 层）。
  CHECK(callbackCount == static_cast<int>(EventBus::kMaxPublishDepth));

  // 超限调用返回后深度已归零：后续普通 publish 仍正常。
  recurse = false;
  callbackCount = 0;
  bus.publish(DepthEvent{});
  CHECK(callbackCount == 1);
}

TEST_CASE("event bus（重入）: 跨事件类型嵌套共享同一深度上限", "[event][event_bus][reentrancy][depth][assert]") {
  EventBus bus;
  int aCount = 0;
  int bCount = 0;
  const Subscription a = bus.subscribe<DepthAEvent>([&](const DepthAEvent&) {
    ++aCount;
    bus.publish(DepthBEvent{});
  });
  const Subscription b = bus.subscribe<DepthBEvent>([&](const DepthBEvent&) {
    ++bCount;
    bus.publish(DepthAEvent{});
  });

#if YR_ENABLE_ASSERTS
  g_depthAssertCount = 0;
  yr::core::setAssertHandler(depthCountingHandler);
#endif
  bus.publish(DepthAEvent{});
#if YR_ENABLE_ASSERTS
  yr::core::setAssertHandler(nullptr);
  CHECK(g_depthAssertCount == 1);
#endif

  // A/B 交替嵌套：层 1..8 共 8 次回调。若深度按类型分别计数，两种类型会各自跑满 8 层。
  CHECK(aCount == 4);
  CHECK(bCount == 4);
  CHECK(aCount + bCount == static_cast<int>(EventBus::kMaxPublishDepth));
}