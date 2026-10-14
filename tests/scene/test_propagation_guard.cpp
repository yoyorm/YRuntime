#include <catch2/catch_test_macros.hpp>

#include <yr/core/assert.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace {

  int g_assertCount = 0;

  yr::core::AssertAction continueAssertHandler(const yr::core::AssertInfo& /*info*/) noexcept {
    ++g_assertCount;
    return yr::core::AssertAction::kContinue;
  }

  // 把全局 assert handler 换成“记录并继续”，析构时恢复默认。
  // 用它验证：即使断言被降级为 kContinue（或 Release 下宏为空），
  // 传播中修改结构的尝试也只返回失败，不会消费 unique_ptr 或破坏树结构。
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

  // 可挂生命周期钩子的节点：钩子只处理当前节点自己，便于在回调里尝试结构修改。
  class HookNode : public yr::scene::Node {
  public:
    using Hook = std::function<void(HookNode&)>;

    Hook onEnter;
    Hook onReadyHook;
    Hook onExit;

    int enterCount = 0;
    int readyCount = 0;
    int exitCount = 0;

  protected:
    void onEnterTree() override {
      ++enterCount;
      if (onEnter) {
        onEnter(*this);
      }
    }

    void onReady() override {
      ++readyCount;
      if (onReadyHook) {
        onReadyHook(*this);
      }
    }

    void onExitTree() override {
      ++exitCount;
      if (onExit) {
        onExit(*this);
      }
    }
  };

} // namespace

TEST_CASE("propagation guard: node may addChild to itself in onEnterTree; new child enters and readies once",
          "[scene][propagation][guard][allow]") {
  auto root = std::make_unique<HookNode>();
  auto pending = std::make_unique<HookNode>();
  HookNode* const rawRoot = root.get();
  HookNode* const rawPending = pending.get();

  bool added = false;
  root->onEnter = [&](HookNode& self) { added = self.addChild(std::move(pending)); };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  CHECK(added);
  CHECK(pending == nullptr); // 所有权已移入 root
  CHECK(rawRoot->childCount() == 1);
  CHECK(rawRoot->childAt(0) == rawPending);
  CHECK(rawPending->parent() == rawRoot);
  // 新 child 本轮 enter 一次；addChild 时 root 尚未 ready，故由随后的
  // propagateReady 统一补上 ready（仍只一次）。
  CHECK(rawPending->enterCount == 1);
  CHECK(rawPending->readyCount == 1);
  CHECK(rawRoot->readyCount == 1);
  CHECK(rawPending->isInsideTree());
  CHECK(rawPending->tree() == &tree);
}

TEST_CASE("propagation guard: node may addChild to itself in onReady; new child enters and readies immediately",
          "[scene][propagation][guard][allow]") {
  auto root = std::make_unique<HookNode>();
  auto pending = std::make_unique<HookNode>();
  HookNode* const rawRoot = root.get();
  HookNode* const rawPending = pending.get();

  bool added = false;
  root->onReadyHook = [&](HookNode& self) { added = self.addChild(std::move(pending)); };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  CHECK(added);
  CHECK(pending == nullptr);
  CHECK(rawRoot->readyCount == 1); // 旧节点不重复 ready
  CHECK(rawRoot->childCount() == 1);
  CHECK(rawPending->parent() == rawRoot);
  // root 此刻已 ready：新 child 入树即 enter + ready。
  CHECK(rawPending->enterCount == 1);
  CHECK(rawPending->readyCount == 1);
  CHECK(rawPending->isInsideTree());
  CHECK(rawPending->tree() == &tree);
}

TEST_CASE("propagation guard: child onEnterTree cannot addChild to parent; rejected without consuming",
          "[scene][propagation][guard][reject]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<HookNode>();
  auto child = std::make_unique<HookNode>();
  // 用基类 unique_ptr 传参：addChild 的形参正是 unique_ptr<Node>&&，
  // 这样失败路径不会在调用点产生“派生→基类”转换临时对象（那会提前 move 走）。
  std::unique_ptr<yr::scene::Node> pending = std::make_unique<yr::scene::Node>();
  HookNode* const rawRoot = root.get();
  HookNode* const rawChild = child.get();
  yr::scene::Node* const rawPending = pending.get();
  REQUIRE(rawRoot->addChild(std::move(child)));

  bool tried = false;
  bool added = true;
  rawChild->onEnter = [&](HookNode& self) {
    tried = true;
    added = self.parent()->addChild(std::move(pending));
  };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  CHECK(tried);
  CHECK_FALSE(added);
  REQUIRE(pending != nullptr); // 失败不消费
  CHECK(pending.get() == rawPending);
  CHECK(rawPending->parent() == nullptr);
  CHECK_FALSE(rawPending->isInsideTree());

  // parent 结构不变。
  CHECK(rawRoot->childCount() == 1);
  CHECK(rawRoot->childAt(0) == rawChild);
  CHECK(rawChild->parent() == rawRoot);
  CHECK(rawChild->isInsideTree());

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("propagation guard: child onReady cannot removeChild from parent; sibling stays in tree",
          "[scene][propagation][guard][reject]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<HookNode>();
  auto sibling = std::make_unique<HookNode>();
  auto child = std::make_unique<HookNode>();
  HookNode* const rawRoot = root.get();
  HookNode* const rawSibling = sibling.get();
  HookNode* const rawChild = child.get();
  REQUIRE(rawRoot->addChild(std::move(sibling)));
  REQUIRE(rawRoot->addChild(std::move(child)));

  bool tried = false;
  std::unique_ptr<yr::scene::Node> removed;
  rawChild->onReadyHook = [&](HookNode& self) {
    tried = true;
    removed = self.parent()->removeChild(rawSibling);
  };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  CHECK(tried);
  CHECK(removed == nullptr);
  CHECK(rawRoot->childCount() == 2);
  CHECK(rawRoot->childAt(0) == rawSibling);
  CHECK(rawRoot->childAt(1) == rawChild);
  CHECK(rawSibling->parent() == rawRoot);
  CHECK(rawSibling->isInsideTree());
  CHECK(rawSibling->tree() == &tree);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("propagation guard: child detach in onEnterTree is rejected; node stays in parent and tree",
          "[scene][propagation][guard][reject]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<HookNode>();
  auto child = std::make_unique<HookNode>();
  HookNode* const rawRoot = root.get();
  HookNode* const rawChild = child.get();
  REQUIRE(rawRoot->addChild(std::move(child)));

  bool tried = false;
  std::unique_ptr<yr::scene::Node> detached;
  rawChild->onEnter = [&](HookNode& self) {
    tried = true;
    detached = self.detach();
  };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  CHECK(tried);
  CHECK(detached == nullptr);
  CHECK(rawChild->parent() == rawRoot);
  CHECK(rawRoot->childCount() == 1);
  CHECK(rawRoot->childAt(0) == rawChild);
  CHECK(rawChild->isInsideTree());
  CHECK(rawChild->tree() == &tree);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("propagation guard: onExitTree freezes own structure for add/remove; after exit leaves tree",
          "[scene][propagation][guard][reject][exit]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<HookNode>();
  auto child = std::make_unique<HookNode>();
  std::unique_ptr<yr::scene::Node> pending = std::make_unique<yr::scene::Node>();
  HookNode* const rawRoot = root.get();
  HookNode* const rawChild = child.get();
  yr::scene::Node* const rawPending = pending.get();
  REQUIRE(rawRoot->addChild(std::move(child)));

  bool addTried = false;
  bool added = true;
  bool removeTried = false;
  std::unique_ptr<yr::scene::Node> removed;
  rawRoot->onExit = [&](HookNode& self) {
    addTried = true;
    added = self.addChild(std::move(pending));
    removeTried = true;
    removed = self.removeChild(rawChild);
  };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  std::unique_ptr<yr::scene::Node> returned = tree.clearRoot();
  REQUIRE(returned.get() == rawRoot);

  CHECK(addTried);
  CHECK_FALSE(added);
  REQUIRE(pending != nullptr); // 失败不消费
  CHECK(pending.get() == rawPending);
  CHECK(rawPending->parent() == nullptr);
  CHECK_FALSE(rawPending->isInsideTree());

  CHECK(removeTried);
  CHECK(removed == nullptr);
  CHECK(rawRoot->childCount() == 1); // 结构不变
  CHECK(rawRoot->childAt(0) == rawChild);
  CHECK(rawChild->parent() == rawRoot);

  // exit 完成后整棵返回子树离树。
  CHECK_FALSE(rawRoot->isInsideTree());
  CHECK(rawRoot->tree() == nullptr);
  CHECK_FALSE(rawChild->isInsideTree());
  CHECK(rawChild->tree() == nullptr);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 2);
#endif
}

TEST_CASE("propagation guard: after a rejected callback, depth returns to zero and normal add/remove succeed",
          "[scene][propagation][guard][recovery]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<HookNode>();
  auto child = std::make_unique<HookNode>();
  std::unique_ptr<yr::scene::Node> rejected = std::make_unique<yr::scene::Node>();
  auto later = std::make_unique<HookNode>();
  HookNode* const rawRoot = root.get();
  HookNode* const rawChild = child.get();
  HookNode* const rawLater = later.get();
  REQUIRE(rawRoot->addChild(std::move(child)));

  bool rejectedAdded = true;
  rawChild->onEnter = [&](HookNode& self) { rejectedAdded = self.parent()->addChild(std::move(rejected)); };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  CHECK_FALSE(rejectedAdded);
  REQUIRE(rejected != nullptr); // 未消费

  // 传播已结束：propagationDepth_ 应回到 0，普通 add/remove 成功。
  REQUIRE(rawRoot->addChild(std::move(later)));
  CHECK(later == nullptr);
  CHECK(rawLater->parent() == rawRoot);
  CHECK(rawLater->isInsideTree());
  CHECK(rawLater->enterCount == 1);
  CHECK(rawLater->readyCount == 1); // 入树时 root 已 ready

  std::unique_ptr<yr::scene::Node> removed = rawRoot->removeChild(rawChild);
  CHECK(removed.get() == rawChild);
  CHECK(rawChild->parent() == nullptr);
  CHECK_FALSE(rawChild->isInsideTree());
}

// ---------------------------------------------------------------------------
// 结构锁（Node-3.5）：removeChild / addChild 在入口 propagationDepth_ 检查之后
// 立即对接收者 this 加 guard，冻结 this 的 children_ 覆盖整个结构操作。下面
// 的用例在 exit / enter 回调里尝试修改正在被操作的那个 parent，验证一律被拒，
// 且外层操作正常完成、结构正确、无迭代器失效 / UAF（ASan 验证）。
// ---------------------------------------------------------------------------

TEST_CASE("structure lock: exit callback removing an EARLIER sibling is rejected; outer removal completes",
          "[scene][propagation][guard][reject][structure]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<HookNode>();
  auto before = std::make_unique<HookNode>();
  auto target = std::make_unique<HookNode>();
  HookNode* const rawRoot = root.get();
  HookNode* const rawBefore = before.get();
  HookNode* const rawTarget = target.get();
  REQUIRE(rawRoot->addChild(std::move(before))); // index 0
  REQUIRE(rawRoot->addChild(std::move(target))); // index 1

  // target 退出时删它之前的兄弟：若不冻结，erase(index 0) 会让外层指向 target 的
  // 迭代器整体前移，随后的 move/erase 就是 UB。
  bool tried = false;
  std::unique_ptr<yr::scene::Node> nested;
  rawTarget->onExit = [&](HookNode&) {
    tried = true;
    nested = rawRoot->removeChild(rawBefore);
  };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  std::unique_ptr<yr::scene::Node> removed = rawRoot->removeChild(rawTarget);

  CHECK(tried);
  CHECK(nested == nullptr);
  REQUIRE(removed.get() == rawTarget);
  CHECK(rawTarget->parent() == nullptr);
  CHECK_FALSE(rawTarget->isInsideTree());
  CHECK(rawRoot->childCount() == 1);
  CHECK(rawRoot->childAt(0) == rawBefore);
  CHECK(rawBefore->parent() == rawRoot);
  CHECK(rawBefore->isInsideTree());

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("structure lock: exit callback removing a LATER sibling is also rejected (uniform contract)",
          "[scene][propagation][guard][reject][structure]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<HookNode>();
  auto target = std::make_unique<HookNode>();
  auto after = std::make_unique<HookNode>();
  HookNode* const rawRoot = root.get();
  HookNode* const rawTarget = target.get();
  HookNode* const rawAfter = after.get();
  REQUIRE(rawRoot->addChild(std::move(target))); // index 0
  REQUIRE(rawRoot->addChild(std::move(after)));  // index 1

  // 删后面的兄弟当前恰好不会使迭代器失效，但契约是"结构操作期间冻结接收者"，
  // 一律拒绝，而不是只挡会崩的路径。
  bool tried = false;
  std::unique_ptr<yr::scene::Node> nested;
  rawTarget->onExit = [&](HookNode&) {
    tried = true;
    nested = rawRoot->removeChild(rawAfter);
  };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  std::unique_ptr<yr::scene::Node> removed = rawRoot->removeChild(rawTarget);

  CHECK(tried);
  CHECK(nested == nullptr);
  REQUIRE(removed.get() == rawTarget);
  CHECK(rawRoot->childCount() == 1);
  CHECK(rawRoot->childAt(0) == rawAfter);
  CHECK(rawAfter->parent() == rawRoot);
  CHECK(rawAfter->isInsideTree());

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("structure lock: exit callback detaching itself is rejected; outer removal completes",
          "[scene][propagation][guard][reject][structure]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<HookNode>();
  auto target = std::make_unique<HookNode>();
  HookNode* const rawRoot = root.get();
  HookNode* const rawTarget = target.get();
  REQUIRE(rawRoot->addChild(std::move(target)));

  bool tried = false;
  std::unique_ptr<yr::scene::Node> nested;
  rawTarget->onExit = [&](HookNode& self) {
    tried = true;
    nested = self.detach(); // 转发 parent->removeChild(this)
  };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  std::unique_ptr<yr::scene::Node> removed = rawRoot->removeChild(rawTarget);

  CHECK(tried);
  CHECK(nested == nullptr);
  REQUIRE(removed.get() == rawTarget);
  CHECK(rawRoot->childCount() == 0);
  CHECK(rawTarget->parent() == nullptr);
  CHECK_FALSE(rawTarget->isInsideTree());

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("structure lock: exit callback addChild at full capacity is rejected without consuming",
          "[scene][propagation][guard][reject][structure]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<HookNode>();
  HookNode* const rawRoot = root.get();
  std::vector<HookNode*> children;
  // 连续 push_back：容量按 1/2/4/8 增长，加满 8 个后 size == capacity。
  // 此刻若结构锁失效，回调里的 push_back 会扩容并使外层迭代器失效。
  for (int i = 0; i < 8; ++i) {
    auto child = std::make_unique<HookNode>();
    children.push_back(child.get());
    REQUIRE(rawRoot->addChild(std::move(child)));
  }
  HookNode* const rawTarget = children.back();

  // 用基类 unique_ptr 传参，失败路径不会在调用点产生派生→基类转换临时对象。
  std::unique_ptr<yr::scene::Node> pending = std::make_unique<yr::scene::Node>();
  yr::scene::Node* const rawPending = pending.get();
  bool tried = false;
  bool added = true;
  rawTarget->onExit = [&](HookNode&) {
    tried = true;
    added = rawRoot->addChild(std::move(pending));
  };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  std::unique_ptr<yr::scene::Node> removed = rawRoot->removeChild(rawTarget);

  CHECK(tried);
  CHECK_FALSE(added);
  REQUIRE(pending != nullptr); // 失败不消费
  CHECK(pending.get() == rawPending);
  CHECK(rawPending->parent() == nullptr);
  CHECK_FALSE(rawPending->isInsideTree());
  REQUIRE(removed.get() == rawTarget);
  CHECK(rawRoot->childCount() == 7);
  CHECK(rawTarget->parent() == nullptr);
  CHECK_FALSE(rawTarget->isInsideTree());

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("structure lock: new child detaching itself in onEnterTree is rejected; addChild completes",
          "[scene][propagation][guard][reject][structure]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<HookNode>();
  HookNode* const rawRoot = root.get();
  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  auto pending = std::make_unique<HookNode>();
  HookNode* const rawPending = pending.get();
  bool tried = false;
  std::unique_ptr<yr::scene::Node> detached;
  rawPending->onEnter = [&](HookNode& self) {
    tried = true;
    detached = self.detach();
  };

  bool added = rawRoot->addChild(std::move(pending));

  CHECK(tried);
  CHECK(detached == nullptr); // 被拒：detach 返回 nullptr

  // addChild 正常完成，节点正确留在树内（不会被自己的 onEnterTree 摘掉）。
  CHECK(added);
  REQUIRE(pending == nullptr);
  CHECK(rawRoot->childCount() == 1);
  CHECK(rawRoot->childAt(0) == rawPending);
  CHECK(rawPending->parent() == rawRoot);
  CHECK(rawPending->isInsideTree());
  CHECK(rawPending->tree() == &tree);
  CHECK(rawPending->enterCount == 1);
  CHECK(rawPending->readyCount == 1); // 父已 ready，新 child enter 后立即 ready

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("structure lock: after rejections depth returns to zero and normal add/remove succeed",
          "[scene][propagation][guard][recovery][structure]") {
  ScopedContinueAssertHandler handler;

  auto root = std::make_unique<HookNode>();
  auto target = std::make_unique<HookNode>();
  HookNode* const rawRoot = root.get();
  HookNode* const rawTarget = target.get();
  REQUIRE(rawRoot->addChild(std::move(target)));

  // target 退出时试图 detach 自己：被拒。
  bool tried = false;
  std::unique_ptr<yr::scene::Node> nested;
  rawTarget->onExit = [&](HookNode& self) {
    tried = true;
    nested = self.detach();
  };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  std::unique_ptr<yr::scene::Node> removed = rawRoot->removeChild(rawTarget);
  CHECK(tried);
  CHECK(nested == nullptr);
  REQUIRE(removed.get() == rawTarget);

  // 传播已结束：propagationDepth_ 应回到 0，普通结构操作继续成功。
  auto later = std::make_unique<HookNode>();
  HookNode* const rawLater = later.get();
  REQUIRE(rawRoot->addChild(std::move(later)));
  CHECK(later == nullptr);
  CHECK(rawLater->parent() == rawRoot);
  CHECK(rawLater->isInsideTree());
  CHECK(rawLater->enterCount == 1);
  CHECK(rawLater->readyCount == 1);

  std::unique_ptr<yr::scene::Node> removedAgain = rawRoot->removeChild(rawLater);
  CHECK(removedAgain.get() == rawLater);
  CHECK(rawLater->parent() == nullptr);
  CHECK_FALSE(rawLater->isInsideTree());
}