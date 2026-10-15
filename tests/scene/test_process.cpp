#include <catch2/catch_test_macros.hpp>

#include <yr/core/assert.h>
#include <yr/object/object_db.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

  int g_assertCount = 0;

  yr::core::AssertAction continueAssertHandler(const yr::core::AssertInfo& /*info*/) noexcept {
    ++g_assertCount;
    return yr::core::AssertAction::kContinue;
  }

  // 把全局 assert handler 换成“记录并继续”，析构时恢复默认。
  // 用它验证：process 重入 / process 期间 flush 被降级为 kContinue（或 Release 下宏为空）时
  // 也只被安全拒绝，不重复派发、不提前销毁。
  class ScopedContinueAssertHandler {
  public:
    ScopedContinueAssertHandler() {
      g_assertCount = 0;
      yr::core::setAssertHandler(&continueAssertHandler);
    }

    ~ScopedContinueAssertHandler() { yr::core::setAssertHandler(nullptr); }

    ScopedContinueAssertHandler(const ScopedContinueAssertHandler&) = delete;
    ScopedContinueAssertHandler& operator=(const ScopedContinueAssertHandler&) = delete;
  };

  // 每次用例开头 clear()；单线程，仅用于断言 update 顺序。
  std::vector<std::string> g_order;

  // 记录 onEnterTree / onReady / onUpdate 次数，并暴露 onUpdate 注入钩子。
  class ProcessNode : public yr::scene::Node {
  public:
    using Hook = std::function<void(ProcessNode&)>;

    explicit ProcessNode(std::string label) : label_(std::move(label)) {}

    [[nodiscard]] const std::string& label() const noexcept { return label_; }

    Hook onUpdateHook;

    int enterCount = 0;
    int readyCount = 0;
    int updateCount = 0;
    float lastDelta = -1.0f;

  protected:
    void onEnterTree() override { ++enterCount; }

    void onReady() override { ++readyCount; }

    void onUpdate(float deltaTime) override {
      ++updateCount;
      lastDelta = deltaTime;
      g_order.push_back(label_);
      if (onUpdateHook) {
        onUpdateHook(*this);
      }
    }

  private:
    std::string label_;
  };

} // namespace

TEST_CASE("process: dispatches pre-order parent before children, siblings in child order", "[scene][process][order]") {
  g_order.clear();

  auto root = std::make_unique<ProcessNode>("root");
  auto a = std::make_unique<ProcessNode>("a");
  auto b = std::make_unique<ProcessNode>("b");
  auto a1 = std::make_unique<ProcessNode>("a1");
  ProcessNode* const rawRoot = root.get();
  ProcessNode* const rawA = a.get();
  ProcessNode* const rawB = b.get();
  ProcessNode* const rawA1 = a1.get();
  REQUIRE(rawRoot->addChild(std::move(a)));
  REQUIRE(rawRoot->addChild(std::move(b)));
  REQUIRE(rawA->addChild(std::move(a1)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  g_order.clear();

  tree.process(0.5f);

  CHECK(g_order == std::vector<std::string>{"root", "a", "a1", "b"});
  CHECK(rawRoot->updateCount == 1);
  CHECK(rawA->updateCount == 1);
  CHECK(rawA1->updateCount == 1);
  CHECK(rawB->updateCount == 1);
  CHECK(rawA1->lastDelta == 0.5f); // deltaTime 透传
}

TEST_CASE("process: a node added during a callback is not processed this frame, but the next",
          "[scene][process][snapshot]") {
  g_order.clear();

  auto root = std::make_unique<ProcessNode>("root");
  auto spawner = std::make_unique<ProcessNode>("spawner");
  ProcessNode* const rawRoot = root.get();
  ProcessNode* const rawSpawner = spawner.get();
  REQUIRE(rawRoot->addChild(std::move(spawner)));

  auto spawned = std::make_unique<ProcessNode>("spawned");
  ProcessNode* const rawSpawned = spawned.get();

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  bool added = false;
  bool addOk = false;
  rawSpawner->onUpdateHook = [&](ProcessNode&) {
    if (!added) {
      added = true;
      addOk = rawRoot->addChild(std::move(spawned));
    }
  };

  g_order.clear();
  tree.process(0.016f);

  CHECK(added);
  CHECK(addOk);
  CHECK(rawSpawned->updateCount == 0); // 本帧不在快照里
  CHECK(rawSpawned->enterCount == 1);  // 但 addChild 时已立即 enter
  CHECK(rawSpawned->readyCount == 1);  // 且已 ready
  CHECK(g_order == std::vector<std::string>{"root", "spawner"});

  g_order.clear();
  tree.process(0.016f);

  CHECK(rawSpawned->updateCount == 1); // 下一帧才被收集到
  CHECK(g_order == std::vector<std::string>{"root", "spawner", "spawned"});
}

TEST_CASE("process: queueFree self during onUpdate stays alive this frame, freed by flush",
          "[scene][process][queuefree]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<ProcessNode>("root");
  auto child = std::make_unique<ProcessNode>("child");
  ProcessNode* const rawRoot = root.get();
  ProcessNode* const rawChild = child.get();
  const yr::core::ObjectID childId = rawChild->id();
  REQUIRE(rawRoot->addChild(std::move(child)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  rawChild->onUpdateHook = [&](ProcessNode& self) { self.queueFree(); };

  tree.process(0.016f);

  // queueFree 只置标记：本帧该节点已被调用、仍存活、仍在树内（对齐 Godot flush 前仍处理）。
  CHECK(rawChild->updateCount == 1);
  CHECK(rawChild->isQueuedForDeletion());
  CHECK(rawChild->isInsideTree());
  CHECK(database.contains(childId));
  CHECK(database.aliveCount() == baseline + 2);

  tree.flushDeletions();
  CHECK_FALSE(database.contains(childId));
  CHECK(rawRoot->childCount() == 0);

  // 下一帧快照收集不到已注销的节点，process 安全。
  tree.process(0.016f);
  CHECK(rawRoot->updateCount == 2);
  CHECK(database.aliveCount() == baseline + 1);
}

TEST_CASE("process: queueFree parent during onUpdate still processes the rest of the frame snapshot",
          "[scene][process][queuefree]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<ProcessNode>("root");
  auto mid = std::make_unique<ProcessNode>("mid");
  auto leaf = std::make_unique<ProcessNode>("leaf");
  auto sibling = std::make_unique<ProcessNode>("sibling");
  ProcessNode* const rawRoot = root.get();
  ProcessNode* const rawMid = mid.get();
  ProcessNode* const rawLeaf = leaf.get();
  ProcessNode* const rawSibling = sibling.get();
  const yr::core::ObjectID rootId = rawRoot->id();
  REQUIRE(rawMid->addChild(std::move(leaf)));
  REQUIRE(rawRoot->addChild(std::move(mid)));
  REQUIRE(rawRoot->addChild(std::move(sibling)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  rawMid->onUpdateHook = [&](ProcessNode&) { rawRoot->queueFree(); };

  tree.process(0.016f);

  // 快照天然包含整棵树：父被排队后，其余节点本帧仍全部被 process。
  CHECK(rawRoot->updateCount == 1);
  CHECK(rawMid->updateCount == 1);
  CHECK(rawLeaf->updateCount == 1);
  CHECK(rawSibling->updateCount == 1);
  CHECK(rawRoot->isQueuedForDeletion());

  tree.flushDeletions();

  CHECK(tree.root() == nullptr);
  CHECK_FALSE(database.contains(rootId));
  CHECK(database.aliveCount() == baseline); // 整棵子树级联销毁

  // 空树：下一帧 process 安全 no-op。
  tree.process(0.016f);
  CHECK(database.aliveCount() == baseline);
}

TEST_CASE("process: removeChild and discard ownership mid-callback makes later snapshot entries resolve to null",
          "[scene][process][detach][asan]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<ProcessNode>("root");
  auto first = std::make_unique<ProcessNode>("first");
  auto second = std::make_unique<ProcessNode>("second");
  auto grand = std::make_unique<ProcessNode>("grand");
  ProcessNode* const rawRoot = root.get();
  ProcessNode* const rawFirst = first.get();
  ProcessNode* const rawSecond = second.get();
  ProcessNode* const rawGrand = grand.get();
  const yr::core::ObjectID secondId = rawSecond->id();
  const yr::core::ObjectID grandId = rawGrand->id();
  REQUIRE(rawSecond->addChild(std::move(grand)));
  REQUIRE(rawRoot->addChild(std::move(first)));
  REQUIRE(rawRoot->addChild(std::move(second)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  // 回调中摘除并丢弃所有权：second + grand 立即析构、从 ObjectDB 注销。
  // 后续快照条目经 generation 失效解析为 nullptr 被跳过，绝不 UAF（ASan 验证）。
  rawFirst->onUpdateHook = [&](ProcessNode&) { (void)rawRoot->removeChild(rawSecond); };

  tree.process(0.016f);

  CHECK(rawFirst->updateCount == 1);
  CHECK_FALSE(database.contains(secondId));
  CHECK_FALSE(database.contains(grandId));
  CHECK(rawRoot->childCount() == 1);
  CHECK(database.aliveCount() == baseline + 2); // 仅剩 root + first
}

TEST_CASE("process: detach but keep ownership mid-callback is skipped because it left the tree",
          "[scene][process][detach]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<ProcessNode>("root");
  auto first = std::make_unique<ProcessNode>("first");
  auto second = std::make_unique<ProcessNode>("second");
  ProcessNode* const rawRoot = root.get();
  ProcessNode* const rawFirst = first.get();
  ProcessNode* const rawSecond = second.get();
  const yr::core::ObjectID secondId = rawSecond->id();
  REQUIRE(rawRoot->addChild(std::move(first)));
  REQUIRE(rawRoot->addChild(std::move(second)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  // 摘下但把所有权留在测试作用域：second 仍存活，后续快照条目能解析成功，
  // 但已不在树内，必须跳过、不调用 onUpdate。
  std::unique_ptr<yr::scene::Node> kept;
  rawFirst->onUpdateHook = [&](ProcessNode&) { kept = rawRoot->removeChild(rawSecond); };

  tree.process(0.016f);

  CHECK(rawFirst->updateCount == 1);
  CHECK(rawSecond->updateCount == 0); // 解析成功但 !isInsideTree → 跳过
  CHECK(kept != nullptr);
  CHECK(kept.get() == rawSecond);
  CHECK_FALSE(rawSecond->isInsideTree());
  CHECK(database.contains(secondId));
  CHECK(database.aliveCount() == baseline + 3); // root + first + kept second
}

TEST_CASE("process: recursive process from a callback is rejected, outer dispatch completes normally",
          "[scene][process][reentrancy]") {
  ScopedContinueAssertHandler handler;
  g_order.clear();

  auto root = std::make_unique<ProcessNode>("root");
  auto child = std::make_unique<ProcessNode>("child");
  ProcessNode* const rawRoot = root.get();
  ProcessNode* const rawChild = child.get();
  REQUIRE(rawRoot->addChild(std::move(child)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  bool called = false;
  rawChild->onUpdateHook = [&](ProcessNode&) {
    if (!called) {
      called = true;
      tree.process(0.0f); // 重入：断言 + 早退，不得重复派发
    }
  };

  tree.process(0.1f);

  CHECK(called);
  CHECK(rawRoot->updateCount == 1); // 重入未造成第二次派发
  CHECK(rawChild->updateCount == 1);
  CHECK(g_order == std::vector<std::string>{"root", "child"});

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("process: flushDeletions during process is rejected and deferred to the next flush",
          "[scene][process][reentrancy][queuefree]") {
  ScopedContinueAssertHandler handler;
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<ProcessNode>("root");
  auto child = std::make_unique<ProcessNode>("child");
  ProcessNode* const rawRoot = root.get();
  ProcessNode* const rawChild = child.get();
  const yr::core::ObjectID childId = rawChild->id();
  REQUIRE(rawRoot->addChild(std::move(child)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  rawChild->onUpdateHook = [&](ProcessNode& self) {
    self.queueFree();
    tree.flushDeletions(); // process 期间被拒，删除留到下一次 flush
  };

  tree.process(0.1f);

  CHECK(rawChild->isQueuedForDeletion());
  CHECK(rawChild->isInsideTree());
  CHECK(database.contains(childId)); // 未被提前销毁
  CHECK(rawRoot->childCount() == 1);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif

  tree.flushDeletions();
  CHECK_FALSE(database.contains(childId));
  CHECK(database.aliveCount() == baseline + 1);
}

TEST_CASE("process: empty tree is a safe no-op", "[scene][process][empty]") {
  yr::scene::SceneTree tree;
  CHECK(tree.root() == nullptr);

  tree.process(0.016f);
  tree.process(0.0f);

  CHECK(tree.root() == nullptr);
}

TEST_CASE("process: five consecutive frames dispatch in a stable deterministic order",
          "[scene][process][determinism]") {
  g_order.clear();

  auto root = std::make_unique<ProcessNode>("root");
  auto a = std::make_unique<ProcessNode>("a");
  auto b = std::make_unique<ProcessNode>("b");
  auto a1 = std::make_unique<ProcessNode>("a1");
  ProcessNode* const rawRoot = root.get();
  ProcessNode* const rawA = a.get();
  REQUIRE(rawRoot->addChild(std::move(a)));
  REQUIRE(rawRoot->addChild(std::move(b)));
  REQUIRE(rawA->addChild(std::move(a1)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  std::vector<std::vector<std::string>> frames;
  for (int i = 0; i < 5; ++i) {
    g_order.clear();
    tree.process(0.016f);
    frames.push_back(g_order);
  }

  const std::vector<std::string> expected{"root", "a", "a1", "b"};
  for (const std::vector<std::string>& frame : frames) {
    CHECK(frame == expected);
  }
  CHECK(rawRoot->updateCount == 5);
}