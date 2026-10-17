#include <catch2/catch_test_macros.hpp>

#include <yr/engine/engine.h>
#include <yr/engine/frame_stats.h>
#include <yr/engine/main_loop.h>
#include <yr/event/event_bus.h>
#include <yr/object/object_db.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

using yr::engine::Engine;

static_assert(!std::is_copy_constructible_v<Engine>);
static_assert(!std::is_copy_assignable_v<Engine>);
static_assert(!std::is_move_constructible_v<Engine>);
static_assert(!std::is_move_assignable_v<Engine>);

namespace {

  // 本测试专用事件类型，避免与其他测试共享分表。
  struct PingEvent {
    int value = 0;
  };

  // 探针节点：记录 onUpdate，可请求 Engine 退出，供退出路径用例。
  class ProbeNode : public yr::scene::Node {
  public:
    int updateCount = 0;
    Engine* engine = nullptr;
    bool requestQuitOnUpdate = false;

  protected:
    void onUpdate(float /*deltaTime*/) override {
      ++updateCount;
      if (requestQuitOnUpdate && engine != nullptr) {
        engine->mainLoop().requestQuit();
      }
    }
  };

} // namespace

TEST_CASE("engine: run executes exactly maxFrames frames", "[engine][engine][run]") {
  Engine engine;

  engine.run(5);

  CHECK(engine.mainLoop().frameCount() == 5);
}

TEST_CASE("engine: run(0) executes no frames", "[engine][engine][run]") {
  Engine engine;

  engine.run(0);

  CHECK(engine.mainLoop().frameCount() == 0);
}

TEST_CASE("engine: accessors expose tree and bus wired into MainLoop", "[engine][engine][wiring]") {
  Engine engine;

  CHECK(&engine.mainLoop().tree() == &engine.sceneTree());
  CHECK(&engine.mainLoop().events() == &engine.events());

  auto root = std::make_unique<ProbeNode>();
  ProbeNode* const rawRoot = root.get();
  REQUIRE(engine.sceneTree().setRoot(std::move(root)));

  int received = 0;
  const yr::evt::Subscription subscription =
      engine.events().subscribe<PingEvent>([&received](const PingEvent& event) { received = event.value; });
  engine.events().post(PingEvent{7});

  engine.run(1);

  CHECK(rawRoot->updateCount == 1); // SceneTree::process 走的是 Engine 拥有的树
  CHECK(received == 7);             // phase 3 flush 走的是 Engine 拥有的总线
}

TEST_CASE("engine: run stops early when shouldQuit is requested", "[engine][engine][run][quit]") {
  Engine engine;

  auto root = std::make_unique<ProbeNode>();
  ProbeNode* const rawRoot = root.get();
  rawRoot->engine = &engine;
  rawRoot->requestQuitOnUpdate = true;
  REQUIRE(engine.sceneTree().setRoot(std::move(root)));

  engine.run(100);

  CHECK(engine.mainLoop().shouldQuit());
  CHECK(engine.mainLoop().frameCount() == 1);
  CHECK(rawRoot->updateCount == 1);
}

TEST_CASE("engine: owned scene tree releases nodes on destruction", "[engine][engine][lifecycle]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();

  {
    Engine engine;
    auto root = std::make_unique<ProbeNode>();
    REQUIRE(engine.sceneTree().setRoot(std::move(root)));

    engine.run(3);

    CHECK(database.aliveCount() == baseline + 1);
  }

  // Engine 析构逆序：MainLoop 先于 SceneTree，root 在此刻销毁并注销。
  CHECK(database.aliveCount() == baseline);
}

TEST_CASE("engine: run with the real clock meets the frame budget and records stats", "[engine][engine][run][stats]") {
  Engine engine;
  engine.mainLoop().stats().setReportInterval(0); // 测试静默

  engine.run(50);

  CHECK(engine.mainLoop().frameCount() == 50);
  CHECK(engine.mainLoop().stats().frameCount() == 50);
}