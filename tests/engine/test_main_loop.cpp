#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <yr/engine/clock.h>
#include <yr/engine/main_loop.h>
#include <yr/event/event_bus.h>
#include <yr/object/object_db.h>
#include <yr/render/null_backend.h>
#include <yr/render/snapshot.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

using yr::engine::Clock;
using yr::engine::kPhaseCount;
using yr::engine::MainLoop;
using yr::engine::Phase;
using yr::engine::phaseName;
using yr::math::Mat4;
using yr::math::Vec3;

// 引用成员只禁止默认赋值；拷贝构造仍合法，故 MainLoop 必须显式删除四种特殊成员。
static_assert(!std::is_copy_constructible_v<MainLoop>);
static_assert(!std::is_copy_assignable_v<MainLoop>);
static_assert(!std::is_move_constructible_v<MainLoop>);
static_assert(!std::is_move_assignable_v<MainLoop>);

static_assert(kPhaseCount == 14);
static_assert(static_cast<std::uint8_t>(Phase::kBeginFrame) == 0);
static_assert(static_cast<std::uint8_t>(Phase::kEndFrame) == 13);

namespace {

  // 接线行为用例使用的类型化事件，避免与其他用例共享分表。
  struct WiringEvent {
    int value = 0;
  };

  // 接线行为用例使用的节点：暴露 onUpdate 注入钩子并记录收到的 delta。
  class WiringNode : public yr::scene::Node {
  public:
    using Hook = std::function<void(WiringNode&)>;

    Hook onUpdateHook;
    int updateCount = 0;
    float lastDelta = -1.0F;

  protected:
    void onUpdate(float deltaTime) override {
      ++updateCount;
      lastDelta = deltaTime;
      if (onUpdateHook) {
        onUpdateHook(*this);
      }
    }
  };

  // 测试探针：派生并逐个记录阶段调用。阶段方法为 protected virtual，记录顺序不侵入生产代码。
  class RecordingMainLoop : public MainLoop {
  public:
    using MainLoop::MainLoop;

    std::vector<Phase> phases;
    double pollInputDelta = -1.0;

  protected:
    void beginFrame() override {
      phases.push_back(Phase::kBeginFrame);
      MainLoop::beginFrame();
    }
    void pollInput() override {
      phases.push_back(Phase::kPollInput);
      pollInputDelta = delta();
      MainLoop::pollInput();
    }
    void flushMessages() override {
      phases.push_back(Phase::kFlushMessages);
      MainLoop::flushMessages();
    }
    void flushEvents() override {
      phases.push_back(Phase::kFlushEvents);
      MainLoop::flushEvents();
    }
    void pumpAssets() override {
      phases.push_back(Phase::kPumpAssets);
      MainLoop::pumpAssets();
    }
    void prePhysics() override {
      phases.push_back(Phase::kPrePhysics);
      MainLoop::prePhysics();
    }
    void physicsSteps() override {
      phases.push_back(Phase::kPhysicsSteps);
      MainLoop::physicsSteps();
    }
    void processPhase() override {
      phases.push_back(Phase::kProcess);
      MainLoop::processPhase();
    }
    void postProcess() override {
      phases.push_back(Phase::kPostProcess);
      MainLoop::postProcess();
    }
    void applyStructure() override {
      phases.push_back(Phase::kApplyStructure);
      MainLoop::applyStructure();
    }
    void extractRender() override {
      phases.push_back(Phase::kExtractRender);
      MainLoop::extractRender();
    }
    void present() override {
      phases.push_back(Phase::kPresent);
      MainLoop::present();
    }
    void collectGarbage() override {
      phases.push_back(Phase::kCollectGarbage);
      MainLoop::collectGarbage();
    }
    void endFrame() override {
      phases.push_back(Phase::kEndFrame);
      MainLoop::endFrame();
    }
  };

  // 记录每次固定步长回调的计数与参数。
  class PhysicsProbeMainLoop : public MainLoop {
  public:
    using MainLoop::MainLoop;

    int physicsStepCount = 0;
    std::vector<double> physicsDeltas;

  protected:
    void onPhysicsStep(double physicsDelta) override {
      ++physicsStepCount;
      physicsDeltas.push_back(physicsDelta);
    }
  };

  // 脚本时钟：按注入的 steady_clock 时间点序列推进，验证 Clock 的采样与 clamp。
  // 基类构造把 last_ 设为真实 now()，随后 reset() 覆写为 times_[0]。
  class ScriptedClock : public Clock {
  public:
    explicit ScriptedClock(std::vector<std::chrono::steady_clock::time_point> times) : times_(std::move(times)) {
      reset();
    }

  protected:
    std::chrono::steady_clock::time_point now() const noexcept override {
      const std::size_t index = cursor_ < times_.size() ? cursor_ : times_.size() - 1;
      ++cursor_;
      return times_[index];
    }

  private:
    std::vector<std::chrono::steady_clock::time_point> times_;
    mutable std::size_t cursor_ = 0;
  };

} // namespace

TEST_CASE("main_loop: tick runs all 14 phases in the documented order", "[engine][main_loop][phases]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  RecordingMainLoop loop(tree, bus);

  loop.tick(1.0 / 60.0);

  REQUIRE(loop.phases.size() == kPhaseCount);
  for (std::size_t i = 0; i < loop.phases.size(); ++i) {
    CAPTURE(i);
    CHECK(static_cast<std::size_t>(loop.phases[i]) == i);
  }
}

TEST_CASE("main_loop: frameCount tracks ticks and frameIndex is 0-based", "[engine][main_loop][counter]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  CHECK(loop.frameCount() == 0);
  CHECK(loop.frameIndex() == 0);

  loop.tick(1.0 / 60.0);
  loop.tick(1.0 / 60.0);
  loop.tick(1.0 / 60.0);

  CHECK(loop.frameCount() == 3);
  CHECK(loop.frameIndex() == 2);
}

TEST_CASE("main_loop: requestQuit only sets a flag; tick still runs a full frame", "[engine][main_loop][quit]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  RecordingMainLoop loop(tree, bus);

  CHECK_FALSE(loop.shouldQuit());
  loop.requestQuit();
  CHECK(loop.shouldQuit());

  loop.tick(1.0 / 60.0);
  CHECK(loop.phases.size() == kPhaseCount);
  CHECK(loop.frameCount() == 1);
}

TEST_CASE("main_loop: a default loop over an empty tree and local bus ticks safely", "[engine][main_loop][empty]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  loop.tick(1.0 / 60.0);
  loop.tick(1.0 / 60.0);

  CHECK(loop.frameCount() == 2);
  CHECK(&loop.tree() == &tree);
  CHECK(&loop.events() == &bus);
}

TEST_CASE("main_loop: phase names match the architecture table", "[engine][main_loop][phase-name]") {
  CHECK(std::string_view(phaseName(Phase::kBeginFrame)) == "begin_frame");
  CHECK(std::string_view(phaseName(Phase::kFlushEvents)) == "flush_events");
  CHECK(std::string_view(phaseName(Phase::kProcess)) == "process");
  CHECK(std::string_view(phaseName(Phase::kApplyStructure)) == "apply_structure");
  CHECK(std::string_view(phaseName(Phase::kEndFrame)) == "end_frame");
  CHECK(std::string_view(phaseName(Phase::kCount)) == "unknown");
}

TEST_CASE("main_loop: flushEvents dispatches events posted before the tick", "[engine][main_loop][events]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  int received = 0;
  const yr::evt::Subscription sub =
      bus.subscribe<WiringEvent>([&received](const WiringEvent& event) { received = event.value; });

  bus.post(WiringEvent{7});
  CHECK(bus.pending() == 1);

  loop.tick(1.0 / 60.0);

  CHECK(received == 7);
  CHECK(bus.pending() == 0);
}

TEST_CASE("main_loop: an event posted from a flush callback is deferred to the next tick",
          "[engine][main_loop][events][double-buffer]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  std::vector<int> received;
  const yr::evt::Subscription sub = bus.subscribe<WiringEvent>([&received, &bus](const WiringEvent& event) {
    received.push_back(event.value);
    if (event.value == 1) {
      bus.post(WiringEvent{2}); // flush 期间 post：进入下一缓冲，本帧不再派发
    }
  });

  bus.post(WiringEvent{1});
  loop.tick(1.0 / 60.0);

  CHECK(received == std::vector<int>{1});
  CHECK(bus.pending() == 1);

  loop.tick(1.0 / 60.0);

  CHECK(received == std::vector<int>{1, 2});
  CHECK(bus.pending() == 0);
}

TEST_CASE("main_loop: onUpdate receives tick's real delta narrowed to float", "[engine][main_loop][delta]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  auto root = std::make_unique<WiringNode>();
  WiringNode* const rawRoot = root.get();
  REQUIRE(tree.setRoot(std::move(root)));

  const double realDelta = 1.0 / 60.0;
  loop.tick(realDelta);

  CHECK(rawRoot->updateCount == 1);
  CHECK(rawRoot->lastDelta == static_cast<float>(realDelta));
}

TEST_CASE("main_loop: delta() exposes the injected real delta, including inside pollInput",
          "[engine][main_loop][delta]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  RecordingMainLoop loop(tree, bus);

  constexpr double realDelta = 0.025;
  loop.tick(realDelta);

  CHECK(loop.pollInputDelta == Catch::Approx(realDelta));
  CHECK(loop.delta() == Catch::Approx(realDelta));
}

TEST_CASE("main_loop: queueFree in onUpdate is applied in phase 9 before tick returns",
          "[engine][main_loop][apply-structure]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();

  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  auto root = std::make_unique<WiringNode>();
  auto victim = std::make_unique<WiringNode>();
  WiringNode* const rawRoot = root.get();
  WiringNode* const rawVictim = victim.get();
  const yr::core::ObjectID victimId = rawVictim->id();
  REQUIRE(rawRoot->addChild(std::move(victim)));
  REQUIRE(tree.setRoot(std::move(root)));

  rawVictim->onUpdateHook = [](WiringNode& self) { self.queueFree(); };

  loop.tick(0.016);

  // tick 返回即阶段 9 已执行：victim 已从 ObjectDB 注销，不再解引用 rawVictim。
  CHECK_FALSE(database.contains(victimId));
  CHECK(rawRoot->childCount() == 0);
  CHECK(database.aliveCount() == baseline + 1);
}

TEST_CASE("main_loop: setPosition in onUpdate is refreshed in phase 8 before tick returns",
          "[engine][main_loop][post-process]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  auto root = std::make_unique<WiringNode>();
  auto child = std::make_unique<WiringNode>();
  WiringNode* const rawRoot = root.get();
  WiringNode* const rawChild = child.get();
  REQUIRE(rawRoot->addChild(std::move(child)));
  REQUIRE(tree.setRoot(std::move(root)));

  const Vec3 position(1.0F, 2.0F, 3.0F);
  rawChild->onUpdateHook = [position](WiringNode& self) { self.setPosition(position); };

  loop.tick(1.0 / 60.0);

  // 阶段 8 已刷新：父在原点，child 的 global 即 T(position)，无需手动 refresh。
  Mat4 expected;
  expected[12] = position.x;
  expected[13] = position.y;
  expected[14] = position.z;
  CHECK(rawChild->position() == position);
  CHECK(rawChild->globalTransform() == expected);
}

TEST_CASE("main_loop: one tick runs post -> flush -> process -> refresh -> deletion end to end",
          "[engine][main_loop][integration]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();

  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  auto root = std::make_unique<WiringNode>();
  auto mover = std::make_unique<WiringNode>();
  auto victim = std::make_unique<WiringNode>();
  WiringNode* const rawRoot = root.get();
  WiringNode* const rawMover = mover.get();
  WiringNode* const rawVictim = victim.get();
  const yr::core::ObjectID victimId = rawVictim->id();
  REQUIRE(rawRoot->addChild(std::move(mover)));
  REQUIRE(rawRoot->addChild(std::move(victim)));
  REQUIRE(tree.setRoot(std::move(root)));

  const Vec3 position(4.0F, 5.0F, 6.0F);
  int received = 0;
  const yr::evt::Subscription sub = bus.subscribe<WiringEvent>([&](const WiringEvent&) {
    ++received;
    rawMover->setPosition(position); // 数据改动 → 阶段 8 刷新
    rawVictim->queueFree();          // 延迟删除 → 阶段 9 执行
  });
  bus.post(WiringEvent{1});

  loop.tick(1.0 / 60.0);

  const Mat4 moved = rawMover->globalTransform();
  CHECK(received == 1);
  CHECK(rawMover->position() == position);
  CHECK(moved[12] == position.x);
  CHECK(moved[13] == position.y);
  CHECK(moved[14] == position.z);
  CHECK_FALSE(database.contains(victimId));
  CHECK(rawRoot->childCount() == 1);
  CHECK(database.aliveCount() == baseline + 2); // 只剩 root + mover
}

TEST_CASE("main_loop: safety-point exclusion flags are cleared after each phase", "[engine][main_loop][safepoint]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  auto root = std::make_unique<WiringNode>();
  WiringNode* const rawRoot = root.get();
  REQUIRE(tree.setRoot(std::move(root)));

  loop.tick(0.016);

  // 阶段返回后 processing_/refreshing_/flushing_ 均已清零：tick 之外直接调用三个安全点
  // 不再触发互斥断言。
  tree.process(0.016F);
  tree.refreshTransforms();
  tree.flushDeletions();

  CHECK(rawRoot->updateCount == 2); // 一次来自 tick，一次来自直接调用
}

TEST_CASE("main_loop: zero and negative deltas are safe with an empty or populated tree",
          "[engine][main_loop][delta]") {
  {
    yr::scene::SceneTree tree;
    yr::evt::EventBus bus;
    MainLoop loop(tree, bus);

    loop.tick(0.0);
    loop.tick(-1.0);

    CHECK(loop.frameCount() == 2);
  }

  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  auto root = std::make_unique<WiringNode>();
  WiringNode* const rawRoot = root.get();
  REQUIRE(tree.setRoot(std::move(root)));

  loop.tick(0.0);
  loop.tick(-1.0);

  CHECK(rawRoot->updateCount == 2);
  CHECK(rawRoot->lastDelta == -1.0F);
}
// ============================================================================
// Clock：墙钟采样与 clamp
// ============================================================================

TEST_CASE("clock: scripted clock returns successive wall-clock deltas", "[engine][clock]") {
  using std::chrono::milliseconds;
  using TimePoint = std::chrono::steady_clock::time_point;
  const TimePoint base{};
  ScriptedClock ticker({base + milliseconds(0), base + milliseconds(100), base + milliseconds(300)});

  CHECK(ticker.nextDelta() == Catch::Approx(0.1));
  CHECK(ticker.nextDelta() == Catch::Approx(0.2));
}

TEST_CASE("clock: clamps a long stall to maxDelta", "[engine][clock]") {
  using std::chrono::milliseconds;
  using TimePoint = std::chrono::steady_clock::time_point;
  const TimePoint base{};
  ScriptedClock ticker({base + milliseconds(0), base + milliseconds(1000)});

  CHECK(ticker.nextDelta() == Catch::Approx(Clock::kDefaultMaxDelta));
}

TEST_CASE("clock: maxDelta zero yields zero after any gap", "[engine][clock]") {
  Clock ticker(0.0);
  CHECK(ticker.nextDelta() == 0.0);
  CHECK(ticker.nextDelta() == 0.0);
}

// ============================================================================
// 固定步长累加器 / spiral 保护
// ============================================================================

TEST_CASE("main_loop: one fixed physics step per 1/60 frame over 100 frames", "[engine][main_loop][fixed-step]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  PhysicsProbeMainLoop loop(tree, bus);

  const double step = loop.physicsStep();
  REQUIRE(step == 1.0 / 60.0);

  for (int frame = 0; frame < 100; ++frame) {
    CAPTURE(frame);
    loop.tick(step);
    REQUIRE(loop.physicsStepCount == frame + 1); // 每帧严格 1 步：1/60 累加再整减无浮点漏步
    CHECK(loop.accumulator() < step);
  }
  CHECK(loop.physicsStepCount == 100);
}

TEST_CASE("main_loop: scripted deltas produce the hand-computed step count", "[engine][main_loop][fixed-step]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  PhysicsProbeMainLoop loop(tree, bus);

  // 手算：0.005→0；+0.05=0.055→3；+0.016≈0.021→1；+0.033≈0.037→2。合计 6。
  for (const double delta : {0.005, 0.05, 0.016, 0.033}) {
    loop.tick(delta);
  }

  CHECK(loop.physicsStepCount == 6);
  CHECK(loop.accumulator() < loop.physicsStep());
}

TEST_CASE("main_loop: a jittered delta sequence is deterministic", "[engine][main_loop][fixed-step][jitter]") {
  const std::vector<double> jitter = {0.001, 0.001, 0.001, 0.001, 0.001, 0.166, 0.001, 0.001, 0.001, 0.05};

  const auto run = [&jitter]() {
    yr::scene::SceneTree tree;
    yr::evt::EventBus bus;
    PhysicsProbeMainLoop loop(tree, bus);
    for (const double delta : jitter) {
      loop.tick(delta);
    }
    return loop.physicsStepCount;
  };

  const int first = run();
  const int second = run();
  CHECK(first == second); // 同序列两次运行步数完全一致
  // 0.166 那帧累计 0.171s 需 10 步，被 8 步上限截断后 fmod 丢弃整步积压：8+(0+0+0+3)=11。
  CHECK(first == 11);
}

TEST_CASE("main_loop: spiral cap runs 8 steps then discards the backlog", "[engine][main_loop][fixed-step][spiral]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  PhysicsProbeMainLoop loop(tree, bus);

  loop.tick(10.0); // 约 600 步的量
  CHECK(loop.physicsStepCount == 8);
  CHECK(loop.accumulator() < loop.physicsStep());

  loop.tick(loop.physicsStep()); // 下一帧从干净的亚步余量恢复，恰好 1 步
  CHECK(loop.physicsStepCount == 9);
  CHECK(loop.accumulator() < loop.physicsStep());
}

TEST_CASE("main_loop: onPhysicsStep receives the exact fixed step", "[engine][main_loop][fixed-step]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  PhysicsProbeMainLoop loop(tree, bus);

  loop.tick(2.0 / 60.0); // 恰好 2 步

  REQUIRE(loop.physicsDeltas.size() == 2);
  CHECK(loop.physicsDeltas[0] == loop.physicsStep());
  CHECK(loop.physicsDeltas[1] == loop.physicsStep());
}

// ============================================================================
// timeScale / paused
// ============================================================================

TEST_CASE("main_loop: timeScale scales onUpdate delta and physics accumulation", "[engine][main_loop][time-scale]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  PhysicsProbeMainLoop loop(tree, bus);

  auto root = std::make_unique<WiringNode>();
  WiringNode* const rawRoot = root.get();
  REQUIRE(tree.setRoot(std::move(root)));

  loop.setTimeScale(0.5);
  CHECK(loop.timeScale() == 0.5);

  // 注入 2×(1/60)：scale 0.5 后 gameDelta 恰为 1/60，仍每帧 1 步；onUpdate 收到缩放后的 dt。
  for (int frame = 0; frame < 2; ++frame) {
    CAPTURE(frame);
    loop.tick(2.0 / 60.0);
    REQUIRE(loop.physicsStepCount == frame + 1);
    CHECK(loop.accumulator() < loop.physicsStep());
  }

  CHECK(rawRoot->updateCount == 2);
  CHECK(rawRoot->lastDelta == static_cast<float>((2.0 / 60.0) * 0.5));
}

TEST_CASE("main_loop: setTimeScale clamps negative values to zero", "[engine][main_loop][time-scale]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  loop.setTimeScale(-1.0);
  CHECK(loop.timeScale() == 0.0);

  loop.setTimeScale(2.0);
  CHECK(loop.timeScale() == 2.0);
}

TEST_CASE("main_loop: paused zeroes physics and update delta but still flushes events and deletions",
          "[engine][main_loop][paused]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();

  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  PhysicsProbeMainLoop loop(tree, bus);
  loop.setPaused(true);

  auto root = std::make_unique<WiringNode>();
  auto victim = std::make_unique<WiringNode>();
  WiringNode* const rawRoot = root.get();
  WiringNode* const rawVictim = victim.get();
  const yr::core::ObjectID victimId = rawVictim->id();
  REQUIRE(rawRoot->addChild(std::move(victim)));
  REQUIRE(tree.setRoot(std::move(root)));

  int received = 0;
  const yr::evt::Subscription sub =
      bus.subscribe<WiringEvent>([&received](const WiringEvent& event) { received = event.value; });
  bus.post(WiringEvent{42});

  rawVictim->onUpdateHook = [](WiringNode& self) { self.queueFree(); };

  loop.tick(1.0);

  CHECK(loop.isPaused());
  CHECK(loop.physicsStepCount == 0);        // 暂停：物理 0 步
  CHECK(rawRoot->lastDelta == 0.0F);        // 暂停：process 收到 dt=0
  CHECK(rawRoot->updateCount == 1);         // 阶段仍执行
  CHECK(received == 42);                    // 阶段 3 照常
  CHECK_FALSE(database.contains(victimId)); // 阶段 9 照常删除
  CHECK(rawRoot->childCount() == 0);
  CHECK(database.aliveCount() == baseline + 1);
}

TEST_CASE("main_loop: setPaused(false) restores delta and physics", "[engine][main_loop][paused]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  PhysicsProbeMainLoop loop(tree, bus);

  auto root = std::make_unique<WiringNode>();
  WiringNode* const rawRoot = root.get();
  REQUIRE(tree.setRoot(std::move(root)));

  loop.setPaused(true);
  loop.tick(1.0 / 60.0);
  CHECK(rawRoot->lastDelta == 0.0F);
  CHECK(loop.physicsStepCount == 0);

  loop.setPaused(false);
  CHECK_FALSE(loop.isPaused());

  loop.tick(1.0 / 60.0);
  CHECK(rawRoot->lastDelta == static_cast<float>(1.0 / 60.0));
  CHECK(loop.physicsStepCount == 1);
  CHECK(rawRoot->updateCount == 2);
}

TEST_CASE("main_loop: paused does not clobber the configured timeScale", "[engine][main_loop][paused][time-scale]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  PhysicsProbeMainLoop loop(tree, bus);

  auto root = std::make_unique<WiringNode>();
  WiringNode* const rawRoot = root.get();
  REQUIRE(tree.setRoot(std::move(root)));

  loop.setTimeScale(0.25);
  loop.setPaused(true);
  loop.tick(1.0 / 60.0);
  CHECK(rawRoot->lastDelta == 0.0F);
  CHECK(loop.timeScale() == 0.25); // 恢复后应还原，而不是被 paused 改成 0

  loop.setPaused(false);
  loop.tick(1.0 / 60.0);
  CHECK(rawRoot->lastDelta == static_cast<float>((1.0 / 60.0) * 0.25));
}

// ============================================================================
// 阶段 10 extract / 阶段 11 present
// ============================================================================

TEST_CASE("main_loop: extract fills a frame-indexed snapshot even without a backend", "[engine][main_loop][render]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  auto root = std::make_unique<yr::scene::Node>();
  auto child = std::make_unique<yr::scene::Node>();
  REQUIRE(root->addChild(std::move(child)));
  REQUIRE(tree.setRoot(std::move(root)));

  loop.tick(1.0 / 60.0);
  CHECK(loop.snapshot().frame_index == 0);
  CHECK(loop.snapshot().items.size() == 2);

  loop.tick(1.0 / 60.0);
  CHECK(loop.snapshot().frame_index == 1);
  CHECK(loop.snapshot().items.size() == 2);

  const MainLoop& constLoop = loop; // const snapshot() 重载
  CHECK(constLoop.snapshot().items.size() == 2);
}

TEST_CASE("main_loop: present forwards the snapshot to the injected backend deterministically",
          "[engine][main_loop][render]") {
  yr::scene::SceneTree tree;
  yr::evt::EventBus bus;
  MainLoop loop(tree, bus);

  auto root = std::make_unique<yr::scene::Node>();
  auto childA = std::make_unique<yr::scene::Node>();
  auto childB = std::make_unique<yr::scene::Node>();
  REQUIRE(root->addChild(std::move(childA)));
  REQUIRE(root->addChild(std::move(childB)));
  REQUIRE(tree.setRoot(std::move(root)));

  auto backend = yr::render::create_null_backend();
  loop.setBackend(backend.get());

  for (int frame = 0; frame < 4; ++frame) {
    CAPTURE(frame);
    loop.tick(1.0 / 60.0);
    const std::size_t items = loop.snapshot().items.size();
    REQUIRE(items == 3);
    const yr::render::BackendStats stats = backend->last_frame_stats();
    CHECK(stats.items == items);
    CHECK(stats.draw_calls == items);
    CHECK(stats.triangles == items);
  }
}
