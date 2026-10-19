// M4 集成测试：确定性 headless 世界跑 N 帧（Debug/Release 10000，ASan 1000），
// 一次性断言帧数、物理步数、事件派发、中途删除、Transform 刷新、FrameStats 百分位与
// ObjectDB 归零（泄漏）。整条链路走 MainLoop::tick 的真实阶段编排，不手工调安全点。

#include <catch2/catch_test_macros.hpp>

#include <yr/core/object_id.h>
#include <yr/core/timer.h>
#include <yr/engine/frame_stats.h>
#include <yr/engine/main_loop.h>
#include <yr/event/event_bus.h>
#include <yr/math/vec3.h>
#include <yr/object/object_db.h>
#include <yr/render/null_backend.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// CMake 按构建配置注入：默认 10000。头文件单独编译时给同值兜底。
#ifndef YR_INTEGRATION_FRAMES
#define YR_INTEGRATION_FRAMES 10000
#endif

namespace {

  // 集成测试的事件类型，避免与其他测试共享分表。
  struct CounterEvent {
    std::uint64_t frame = 0;
  };

  // 探针节点：记录 onUpdate / onExitTree，暴露注入钩子。
  class ProbeNode : public yr::scene::Node {
  public:
    using Hook = std::function<void(ProbeNode&)>;

    Hook onUpdateHook;
    std::function<void()> onExitHook;
    int updateCount = 0;
    int exitCount = 0;

  protected:
    void onUpdate(float /*deltaTime*/) override {
      ++updateCount;
      if (onUpdateHook) {
        onUpdateHook(*this);
      }
    }

    void onExitTree() override {
      ++exitCount;
      if (onExitHook) {
        onExitHook();
      }
    }
  };

  // 只覆写 onPhysicsStep：base 的 physicsSteps() 仍负责计时与统计，探针独立计步。
  class ProbeMainLoop : public yr::engine::MainLoop {
  public:
    using MainLoop::MainLoop;

    int physicsStepCount = 0;

  protected:
    void onPhysicsStep(double /*physicsDelta*/) override { ++physicsStepCount; }
  };

  // 前序节点计数：用于比对抽取出的快照条目数。
  std::size_t countNodes(const yr::scene::Node& node) {
    std::size_t count = 1;
    for (std::size_t child = 0; child < node.childCount(); ++child) {
      count += countNodes(*node.childAt(child));
    }
    return count;
  }

} // namespace

TEST_CASE("integration: deterministic headless tick is leak-free and stats-sane", "[engine][integration][main_loop]") {
  constexpr std::uint64_t kFrames = YR_INTEGRATION_FRAMES;
  constexpr double kDelta = 1.0 / 60.0;
  constexpr int kChildren = 10;
  constexpr int kGrandchildren = 9;

  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();

  std::vector<std::uint64_t> receivedFrames;
  int deletedVictims = 0;
  yr::core::ObjectID victimId{};

  {
    yr::scene::SceneTree tree;
    yr::evt::EventBus bus;
    ProbeMainLoop loop(tree, bus);
    loop.stats().setReportInterval(0); // 测试静默：不打印周期报告

    // NullBackend 由测试拥有，生命周期覆盖整段运行；Engine 只持非拥有指针。
    auto backend = yr::render::create_null_backend();
    loop.setBackend(backend.get());

    // 确定性树：root + 10 子 + 每子 9 孙，另有 1 个中途自杀的 victim。
    auto root = std::make_unique<ProbeNode>();
    ProbeNode* const rawRoot = root.get();
    std::vector<ProbeNode*> movers;
    movers.reserve(kChildren);

    for (int c = 0; c < kChildren; ++c) {
      auto child = std::make_unique<ProbeNode>();
      ProbeNode* const rawChild = child.get();
      for (int g = 0; g < kGrandchildren; ++g) {
        (void)rawChild->addChild(std::make_unique<ProbeNode>());
      }
      // 取第一个孙节点作为 mover，指针在整段运行期间保持有效（不会被删）。
      ProbeNode* const mover = static_cast<ProbeNode*>(rawChild->childAt(0));
      mover->onUpdateHook = [c](ProbeNode& self) {
        self.setPosition(yr::math::Vec3{static_cast<float>(self.updateCount), static_cast<float>(c), 0.0F});
      };
      movers.push_back(mover);
      (void)rawRoot->addChild(std::move(child));
    }

    auto victim = std::make_unique<ProbeNode>();
    ProbeNode* const rawVictim = victim.get();
    victimId = rawVictim->id();
    rawVictim->onExitHook = [&deletedVictims]() { ++deletedVictims; };
    rawVictim->onUpdateHook = [](ProbeNode& self) {
      if (self.updateCount == static_cast<int>(kFrames / 2)) {
        self.queueFree(); // 本帧阶段 9 执行删除
      }
    };
    (void)rawRoot->addChild(std::move(victim));
    REQUIRE(tree.setRoot(std::move(root)));

    CHECK(rawRoot->childCount() == static_cast<std::size_t>(kChildren + 1));

    const yr::evt::Subscription subscription = bus.subscribe<CounterEvent>(
        [&receivedFrames](const CounterEvent& event) { receivedFrames.push_back(event.frame); });

    for (std::uint64_t frame = 0; frame < kFrames; ++frame) {
      bus.post(CounterEvent{frame}); // 本帧阶段 3 flush 派发
      loop.tick(kDelta);
      REQUIRE_FALSE(loop.shouldQuit());
      // 阶段 10/11：每帧后端统计都等于本帧快照条目数（稳定、无漂移）。
      REQUIRE(backend->last_frame_stats().items == loop.snapshot().items.size());
    }

    // --- RenderSnapshot → NullBackend：条目数等于最终树内节点数 ---
    const std::size_t finalNodes = countNodes(*rawRoot);
    CHECK(finalNodes == static_cast<std::size_t>(kChildren) * (kGrandchildren + 1) + 1);
    CHECK(loop.snapshot().items.size() == finalNodes);
    CHECK(backend->last_frame_stats().items == finalNodes);
    CHECK(backend->last_frame_stats().draw_calls == finalNodes);

    // --- 帧数 / 物理步数 ---
    CHECK(loop.frameCount() == kFrames);
    CHECK(loop.physicsStepCount == static_cast<int>(kFrames)); // 固定 1/60：每帧恰好 1 步
    CHECK(loop.stats().totalPhysicsSteps() == static_cast<int>(kFrames));
    CHECK(loop.accumulator() < loop.physicsStep());

    // --- 事件派发 ---
    CHECK(receivedFrames.size() == kFrames);
    CHECK(bus.pending() == 0);
    REQUIRE_FALSE(receivedFrames.empty());
    CHECK(receivedFrames.front() == 0);
    CHECK(receivedFrames.back() == kFrames - 1);
    for (std::size_t i = 0; i < receivedFrames.size(); ++i) {
      REQUIRE(receivedFrames[i] == i); // 顺序确定
    }

    // --- 中途 queueFree 的节点确实被删 ---
    CHECK(deletedVictims == 1);
    CHECK_FALSE(database.contains(victimId));
    CHECK(rawRoot->childCount() == static_cast<std::size_t>(kChildren));

    // --- Transform 层级刷新：mover 的 global 平移 == 最后一次 position ---
    for (std::size_t i = 0; i < movers.size(); ++i) {
      ProbeNode* const mover = movers[i];
      CHECK(mover->updateCount == static_cast<int>(kFrames));
      const yr::math::Vec3& position = mover->position();
      CHECK(position.x == static_cast<float>(kFrames));
      CHECK(position.y == static_cast<float>(i));
      const yr::math::Mat4 global = mover->globalTransform();
      CHECK(global[12] == position.x);
      CHECK(global[13] == position.y);
      CHECK(global[14] == position.z);
    }

    // --- FrameStats 窗口与百分位 ---
    const yr::engine::FrameStats& stats = loop.stats();
    CHECK(stats.frameCount() == kFrames);
    CHECK(stats.windowCount() == std::min<std::size_t>(kFrames, stats.windowSize()));
    const yr::core::Duration p50 = stats.totalP50();
    const yr::core::Duration p95 = stats.totalP95();
    const yr::core::Duration max = stats.totalMax();
    CHECK(p50 <= p95);
    CHECK(p95 <= max);
    CHECK(max > yr::core::Duration::zero());
    CHECK(max < std::chrono::seconds(1)); // max 不离谱
    // 接线阶段应有真实耗时样本（至少 total / process 非零）。
    CHECK(stats.phaseP95(yr::engine::Phase::kProcess) > yr::core::Duration::zero());

    // 丢弃整棵子树所有权，节点立即析构；下一行之后 ObjectDB 应回到基线。
    { std::unique_ptr<yr::scene::Node> released = tree.clearRoot(); }
    CHECK(database.aliveCount() == baseline);
  }

  // 树已析构：确认没有节点在作用域结束后残留（泄漏断言）。
  CHECK(database.aliveCount() == baseline);
}