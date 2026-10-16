#include <catch2/catch_test_macros.hpp>

#include <yr/core/assert.h>
#include <yr/object/object_db.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>
#include <yr/scene/transform_store.h>

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

using yr::math::Mat4;
using yr::math::Vec3;
using yr::scene::Node;
using yr::scene::SceneTree;
using yr::scene::TransformData;

// M4 第一版 TransformData = global 缓存 + 脏标记，是平凡可拷贝的值类型。
static_assert(std::is_trivially_copyable_v<TransformData>);
static_assert(std::is_default_constructible_v<TransformData>);

// 访问器：const + noexcept。
static_assert(noexcept(std::declval<const Node&>().position()));
static_assert(noexcept(std::declval<const Node&>().globalTransform()));
static_assert(noexcept(std::declval<const SceneTree&>().transformCount()));

namespace {

  int g_assertCount = 0;

  yr::core::AssertAction continueAssertHandler(const yr::core::AssertInfo& /*info*/) noexcept {
    ++g_assertCount;
    return yr::core::AssertAction::kContinue;
  }

  // 把全局 assert handler 换成“记录并继续”，析构时恢复默认。
  // 用它验证：process 期间 refreshTransforms 被降级为 kContinue（或 Release 下宏为空）时
  // 也只被安全拒绝，不会中途刷新出半旧半新的值。
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

  // 在 onUpdate 里调用 refreshTransforms 的节点，用于锁定安全点互斥。
  class RefreshHookNode : public Node {
  public:
    SceneTree* treeToRefresh = nullptr;
    bool updateCalled = false;

  protected:
    void onUpdate(float /*deltaTime*/) override {
      updateCalled = true;
      if (treeToRefresh != nullptr) {
        treeToRefresh->refreshTransforms(); // 期望被拒绝
      }
    }
  };

} // namespace

TEST_CASE("transform: single node default position refreshes to identity", "[scene][transform]") {
  auto root = std::make_unique<Node>();
  Node* const rawRoot = root.get();

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  CHECK(rawRoot->position() == Vec3(0.0F, 0.0F, 0.0F));
  tree.refreshTransforms();
  CHECK(rawRoot->globalTransform() == Mat4::identity());
}

TEST_CASE("transform: setPosition then refresh yields a pure translation", "[scene][transform]") {
  auto root = std::make_unique<Node>();
  Node* const rawRoot = root.get();

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  rawRoot->setPosition(Vec3(1.0F, 2.0F, 3.0F));
  tree.refreshTransforms();

  // transformPoint 验证原点被平移到 position（含平移分量）。
  CHECK(rawRoot->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 2.0F, 3.0F));
  // 方向向量不受平移影响。
  CHECK(rawRoot->globalTransform().transformVector(Vec3(1.0F, 0.0F, 0.0F)) == Vec3(1.0F, 0.0F, 0.0F));
}

TEST_CASE("transform: three-level hierarchy accumulates parent positions", "[scene][transform][hierarchy]") {
  auto root = std::make_unique<Node>();
  auto child = std::make_unique<Node>();
  auto grandchild = std::make_unique<Node>();
  Node* const rawRoot = root.get();
  Node* const rawChild = child.get();
  Node* const rawGrandchild = grandchild.get();
  REQUIRE(rawRoot->addChild(std::move(child)));
  REQUIRE(rawChild->addChild(std::move(grandchild)));

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  rawRoot->setPosition(Vec3(1.0F, 0.0F, 0.0F));
  rawChild->setPosition(Vec3(0.0F, 2.0F, 0.0F));
  rawGrandchild->setPosition(Vec3(0.0F, 0.0F, 3.0F));
  tree.refreshTransforms();

  CHECK(rawGrandchild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 2.0F, 3.0F));
  CHECK(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 2.0F, 0.0F));
}

TEST_CASE("transform: refresh only touches dirty subtrees", "[scene][transform][dirty]") {
  auto root = std::make_unique<Node>();
  auto child = std::make_unique<Node>();
  Node* const rawRoot = root.get();
  Node* const rawChild = child.get();
  REQUIRE(rawRoot->addChild(std::move(child)));

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  rawRoot->setPosition(Vec3(10.0F, 0.0F, 0.0F));
  rawChild->setPosition(Vec3(1.0F, 0.0F, 0.0F));
  tree.refreshTransforms();

  const Mat4 rootBefore = rawRoot->globalTransform();
  const Mat4 childBefore = rawChild->globalTransform();
  REQUIRE(childBefore.transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(11.0F, 0.0F, 0.0F));

  // 只改叶子：父未标脏，refresh 后父的 global 不变，叶子按父的缓存重算。
  rawChild->setPosition(Vec3(2.0F, 0.0F, 0.0F));
  tree.refreshTransforms();
  CHECK(rawRoot->globalTransform() == rootBefore);
  CHECK(rawChild->globalTransform() != childBefore);
  CHECK(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(12.0F, 0.0F, 0.0F));

  // 改父：整棵子树随父重算。
  rawRoot->setPosition(Vec3(20.0F, 0.0F, 0.0F));
  tree.refreshTransforms();
  CHECK(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(22.0F, 0.0F, 0.0F));
}

TEST_CASE("transform: globalTransform returns the last refreshed value, not pending writes",
          "[scene][transform][read-contract]") {
  auto root = std::make_unique<Node>();
  Node* const rawRoot = root.get();

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  tree.refreshTransforms(); // 先算一次，得到确定的旧值

  rawRoot->setPosition(Vec3(5.0F, 0.0F, 0.0F));
  // set 后、refresh 前：读到旧值（identity）。
  CHECK(rawRoot->globalTransform() == Mat4::identity());

  tree.refreshTransforms();
  CHECK(rawRoot->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(5.0F, 0.0F, 0.0F));
}

TEST_CASE("transform: detached subtree keeps old global and recomputes on re-entry", "[scene][transform][detach]") {
  auto root = std::make_unique<Node>();
  auto parentA = std::make_unique<Node>();
  auto parentB = std::make_unique<Node>();
  auto child = std::make_unique<Node>();
  Node* const rawRoot = root.get();
  Node* const rawParentA = parentA.get();
  Node* const rawParentB = parentB.get();
  Node* const rawChild = child.get();
  REQUIRE(rawRoot->addChild(std::move(parentA)));
  REQUIRE(rawRoot->addChild(std::move(parentB)));
  REQUIRE(rawParentA->addChild(std::move(child)));

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  rawParentA->setPosition(Vec3(1.0F, 0.0F, 0.0F));
  rawChild->setPosition(Vec3(0.0F, 2.0F, 0.0F));
  tree.refreshTransforms();
  REQUIRE(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 2.0F, 0.0F));

  // detach：保留槽与旧 global，不重算。
  std::unique_ptr<Node> detached = rawChild->detach();
  REQUIRE(detached.get() == rawChild);
  CHECK_FALSE(rawChild->isInsideTree());
  CHECK(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 2.0F, 0.0F));

  // 离线 set 标脏；重新挂到 B 后 refresh 按新父链重算。
  rawChild->setPosition(Vec3(0.0F, 0.0F, 3.0F));
  rawParentB->setPosition(Vec3(10.0F, 0.0F, 0.0F));
  REQUIRE(rawParentB->addChild(std::move(detached)));
  tree.refreshTransforms();

  CHECK(rawChild->isInsideTree());
  CHECK(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(10.0F, 0.0F, 3.0F));
}

TEST_CASE("transform: re-entry recomputes even without an intervening setPosition",
          "[scene][transform][detach][reentry]") {
  auto root = std::make_unique<Node>();
  auto parentA = std::make_unique<Node>();
  auto parentB = std::make_unique<Node>();
  auto child = std::make_unique<Node>();
  Node* const rawRoot = root.get();
  Node* const rawParentA = parentA.get();
  Node* const rawParentB = parentB.get();
  Node* const rawChild = child.get();
  REQUIRE(rawRoot->addChild(std::move(parentA)));
  REQUIRE(rawRoot->addChild(std::move(parentB)));
  REQUIRE(rawParentA->addChild(std::move(child)));

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  rawParentA->setPosition(Vec3(1.0F, 0.0F, 0.0F));
  rawChild->setPosition(Vec3(0.0F, 0.0F, 0.0F));
  rawParentB->setPosition(Vec3(10.0F, 0.0F, 0.0F));
  tree.refreshTransforms();

  // 不 set、直接换父：入树必须标脏，否则会保留 A 下的旧 global（1,0,0）。
  std::unique_ptr<Node> detached = rawChild->detach();
  REQUIRE(rawParentB->addChild(std::move(detached)));
  tree.refreshTransforms();

  CHECK(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(10.0F, 0.0F, 0.0F));
}

TEST_CASE("transform: flushDeletions reclaims transform slots of the freed subtree", "[scene][transform][queuefree]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();

  auto root = std::make_unique<Node>();
  auto child = std::make_unique<Node>();
  auto grandchild = std::make_unique<Node>();
  Node* const rawRoot = root.get();
  Node* const rawChild = child.get();
  REQUIRE(rawChild->addChild(std::move(grandchild)));
  REQUIRE(rawRoot->addChild(std::move(child)));

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  CHECK(tree.transformCount() == 3);

  rawChild->queueFree();
  tree.flushDeletions();

  CHECK(tree.transformCount() == 1); // child + grandchild 的槽随子树析构归还
  CHECK(database.aliveCount() == baseline + 1);

  // 槽复用：新建节点仍能正常分配、刷新。
  auto replacement = std::make_unique<Node>();
  Node* const rawReplacement = replacement.get();
  REQUIRE(rawRoot->addChild(std::move(replacement)));
  CHECK(tree.transformCount() == 2);

  rawReplacement->setPosition(Vec3(7.0F, 0.0F, 0.0F));
  tree.refreshTransforms();
  CHECK(rawReplacement->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(7.0F, 0.0F, 0.0F));
}

TEST_CASE("transform: empty tree refresh is a no-op and repeated refresh is idempotent", "[scene][transform][empty]") {
  SceneTree tree;
  tree.refreshTransforms(); // 空树安全
  CHECK(tree.transformCount() == 0);

  auto root = std::make_unique<Node>();
  Node* const rawRoot = root.get();
  REQUIRE(tree.setRoot(std::move(root)));

  rawRoot->setPosition(Vec3(1.0F, 2.0F, 3.0F));
  tree.refreshTransforms();
  const Mat4 first = rawRoot->globalTransform();

  tree.refreshTransforms(); // 第二次：无脏节点，结果不变
  tree.refreshTransforms();
  CHECK(rawRoot->globalTransform() == first);
  CHECK(first.transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 2.0F, 3.0F));
}

TEST_CASE("transform: removing and destroying a node releases only on destruction",
          "[scene][transform][lifetime][asan]") {
  auto root = std::make_unique<Node>();
  Node* const rawRoot = root.get();

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  CHECK(tree.transformCount() == 1);

  auto child = std::make_unique<Node>();
  Node* const rawChild = child.get();
  REQUIRE(rawRoot->addChild(std::move(child)));
  CHECK(tree.transformCount() == 2);

  std::unique_ptr<Node> removed = rawRoot->removeChild(rawChild);
  CHECK(tree.transformCount() == 2); // detach 保留槽（读取契约需要旧 global）

  removed.reset();
  CHECK(tree.transformCount() == 1); // 节点析构才归还槽
}

TEST_CASE("transform: moving a node to another SceneTree migrates its slot", "[scene][transform][lifetime][reparent]") {
  auto node = std::make_unique<Node>();
  Node* const raw = node.get();
  raw->setPosition(Vec3(1.0F, 0.0F, 0.0F)); // 离线 set，首次入树随槽生效

  SceneTree treeA;
  REQUIRE(treeA.setRoot(std::move(node)));
  CHECK(treeA.transformCount() == 1);
  treeA.refreshTransforms();
  CHECK(raw->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 0.0F, 0.0F));

  std::unique_ptr<Node> moved = treeA.clearRoot();
  REQUIRE(moved.get() == raw);
  CHECK(treeA.transformCount() == 1); // 出树保留槽

  SceneTree treeB;
  REQUIRE(treeB.setRoot(std::move(moved)));
  CHECK(treeA.transformCount() == 0); // 跨树迁移：归还旧树槽
  CHECK(treeB.transformCount() == 1); // 在新树分配
  treeB.refreshTransforms();
  CHECK(raw->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 0.0F, 0.0F));
}

TEST_CASE("transform: refreshTransforms during process is rejected", "[scene][transform][reentrancy]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<RefreshHookNode>();
  RefreshHookNode* const rawRoot = root.get();
  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  rawRoot->treeToRefresh = &tree;

  rawRoot->setPosition(Vec3(3.0F, 0.0F, 0.0F));
  tree.process(0.0F); // onUpdate 里的 refresh 必须被拒

  CHECK(rawRoot->updateCalled);
  // 被拒：refresh 未执行，仍读到 refresh 前值（identity）。
  CHECK(rawRoot->globalTransform() == Mat4::identity());

  tree.refreshTransforms();
  CHECK(rawRoot->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(3.0F, 0.0F, 0.0F));

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("transform: failed setRoot leaves the existing root and slot count untouched",
          "[scene][transform][setroot][failure]") {
  ScopedContinueAssertHandler handler;

  auto firstRoot = std::make_unique<Node>();
  Node* const rawFirstRoot = firstRoot.get();
  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(firstRoot)));

  auto secondRoot = std::make_unique<Node>();
  Node* const rawSecondRoot = secondRoot.get();
  rawSecondRoot->setPosition(Vec3(9.0F, 9.0F, 9.0F));

  // 已有 root：失败且不消费参数（所有权仍在调用者手里）。
  CHECK_FALSE(tree.setRoot(std::move(secondRoot)));
  CHECK(secondRoot != nullptr);
  CHECK(secondRoot.get() == rawSecondRoot);

  // 原 root、其 transform 槽与全局状态不受影响；第二个节点仍是离线、无槽。
  CHECK(tree.root() == rawFirstRoot);
  CHECK(tree.transformCount() == 1);
  CHECK_FALSE(rawSecondRoot->isInsideTree());
  CHECK(rawSecondRoot->globalTransform() == Mat4::identity());

  tree.refreshTransforms();
  CHECK(rawFirstRoot->globalTransform() == Mat4::identity());

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("transform: queueFree of a leaf reclaims its transform slot on flush",
          "[scene][transform][queuefree][leaf]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();

  auto root = std::make_unique<Node>();
  auto leaf = std::make_unique<Node>();
  Node* const rawRoot = root.get();
  Node* const rawLeaf = leaf.get();
  REQUIRE(rawRoot->addChild(std::move(leaf)));

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  CHECK(tree.transformCount() == 2);
  CHECK(rawLeaf->childCount() == 0); // 孤立叶子：没有子节点可级联

  rawLeaf->queueFree();
  tree.flushDeletions();

  CHECK(tree.transformCount() == 1); // 仅叶子的槽被回收
  CHECK(rawRoot->childCount() == 0);
  CHECK(database.aliveCount() == baseline + 1);
}

TEST_CASE("transform: in-tree node reports identity before the first refresh", "[scene][transform][first-refresh]") {
  auto root = std::make_unique<Node>();
  auto child = std::make_unique<Node>();
  Node* const rawRoot = root.get();
  Node* const rawChild = child.get();
  REQUIRE(rawRoot->addChild(std::move(child)));

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  // 槽在入树时已分配，但一次 refresh 都没跑过：缓存仍是初始 identity。
  CHECK(tree.transformCount() == 2);
  rawRoot->setPosition(Vec3(1.0F, 0.0F, 0.0F));
  rawChild->setPosition(Vec3(0.0F, 2.0F, 0.0F));
  CHECK(rawRoot->globalTransform() == Mat4::identity());
  CHECK(rawChild->globalTransform() == Mat4::identity());

  // 首次 refresh 才产生真实值。
  tree.refreshTransforms();
  CHECK(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 2.0F, 0.0F));
}

TEST_CASE("transform: a ten-level chain accumulates every ancestor position in one refresh",
          "[scene][transform][hierarchy][deep]") {
  constexpr int kDepth = 10;
  auto root = std::make_unique<Node>();
  Node* parent = root.get();
  std::vector<Node*> chain;
  chain.reserve(kDepth);
  chain.push_back(parent);
  for (int i = 1; i < kDepth; ++i) {
    auto node = std::make_unique<Node>();
    Node* const raw = node.get();
    REQUIRE(parent->addChild(std::move(node)));
    chain.push_back(raw);
    parent = raw;
  }

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  CHECK(tree.transformCount() == static_cast<std::size_t>(kDepth));

  for (Node* node : chain) {
    node->setPosition(Vec3(1.0F, 2.0F, 3.0F));
  }
  tree.refreshTransforms();

  const Vec3 expected = Vec3(1.0F, 2.0F, 3.0F) * static_cast<float>(kDepth);
  CHECK(chain.back()->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == expected);
}

TEST_CASE("transform: re-adding to the same parent re-marks dirty and picks up a moved parent",
          "[scene][transform][detach][reentry][same-parent]") {
  auto root = std::make_unique<Node>();
  auto parent = std::make_unique<Node>();
  auto child = std::make_unique<Node>();
  Node* const rawRoot = root.get();
  Node* const rawParent = parent.get();
  Node* const rawChild = child.get();
  REQUIRE(rawRoot->addChild(std::move(parent)));
  REQUIRE(rawParent->addChild(std::move(child)));

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  rawParent->setPosition(Vec3(1.0F, 0.0F, 0.0F));
  rawChild->setPosition(Vec3(0.0F, 2.0F, 0.0F));
  tree.refreshTransforms();
  REQUIRE(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 2.0F, 0.0F));

  // detach（同树、同 owner）后改变父的 position，并先 refresh 一次让父变 clean。
  std::unique_ptr<Node> detached = rawChild->detach();
  REQUIRE(detached.get() == rawChild);
  CHECK(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 2.0F, 0.0F));

  rawParent->setPosition(Vec3(10.0F, 0.0F, 0.0F));
  tree.refreshTransforms(); // 父在此变 clean；子不在树内，保持旧值
  CHECK(rawParent->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(10.0F, 0.0F, 0.0F));
  CHECK(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 2.0F, 0.0F));

  // 挂回同一父：同 owner 保留槽，但必须标脏。此时父已 clean，只有子的脏能驱动重算，
  // 因此若重新入树漏标脏，下面会读到旧值 (1,2,0) 而失败。
  REQUIRE(rawParent->addChild(std::move(detached)));
  CHECK(tree.transformCount() == 3);
  tree.refreshTransforms();
  CHECK(rawChild->globalTransform().transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(10.0F, 2.0F, 0.0F));
}