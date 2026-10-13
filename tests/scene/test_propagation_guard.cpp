#include <catch2/catch_test_macros.hpp>

#include <yr/core/assert.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

#include <functional>
#include <memory>
#include <utility>

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