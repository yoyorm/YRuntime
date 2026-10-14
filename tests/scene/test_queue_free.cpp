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

// 访问器：const + noexcept。
static_assert(noexcept(std::declval<const yr::scene::Node&>().isQueuedForDeletion()));

namespace {

  int g_assertCount = 0;

  yr::core::AssertAction continueAssertHandler(const yr::core::AssertInfo& /*info*/) noexcept {
    ++g_assertCount;
    return yr::core::AssertAction::kContinue;
  }

  // 把全局 assert handler 换成“记录并继续”，析构时恢复默认。
  // 用它验证：即使断言被降级为 kContinue（或 Release 下宏为空），
  // 非树内 queueFree / 孤儿节点也只被安全跳过，不消费、不误删。
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

  // 每次用例开头 clear()；单线程，仅用于断言 exit 顺序。
  std::vector<std::string> g_order;

  // 可注入 ready / exit 钩子的节点：钩子在生命周期回调里执行（此时节点仍存活）。
  class QueueNode : public yr::scene::Node {
  public:
    using Hook = std::function<void(QueueNode&)>;

    explicit QueueNode(std::string label) : label_(std::move(label)) {}

    [[nodiscard]] const std::string& label() const noexcept { return label_; }

    Hook onReadyHook;
    Hook onExitHook;

    int readyCount = 0;
    int exitCount = 0;

  protected:
    void onReady() override {
      ++readyCount;
      if (onReadyHook) {
        onReadyHook(*this);
      }
    }

    void onExitTree() override {
      ++exitCount;
      g_order.push_back(label_ + ":exit");
      if (onExitHook) {
        onExitHook(*this);
      }
    }

  private:
    std::string label_;
  };

} // namespace

TEST_CASE("queueFree: self-queue in onReady keeps node in tree until flush, then frees once",
          "[scene][queuefree][ready]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<QueueNode>("root");
  auto child = std::make_unique<QueueNode>("child");
  QueueNode* const rawRoot = root.get();
  QueueNode* const rawChild = child.get();
  const yr::core::ObjectID childId = rawChild->id();
  REQUIRE(rawRoot->addChild(std::move(child)));

  int childExitCount = 0;
  bool queuedInReady = false;
  rawChild->onExitHook = [&](QueueNode&) { ++childExitCount; };
  rawChild->onReadyHook = [&](QueueNode& self) {
    queuedInReady = true;
    self.queueFree();
  };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  // queueFree 不改结构、不触发回调：flush 前节点仍在树内、仍可访问。
  CHECK(queuedInReady);
  CHECK(rawChild->isQueuedForDeletion());
  CHECK(rawChild->isInsideTree());
  CHECK(rawRoot->childCount() == 1);
  CHECK(rawRoot->childAt(0) == rawChild);
  CHECK(childExitCount == 0);
  CHECK(database.contains(childId));

  tree.flushDeletions();

  CHECK_FALSE(database.contains(childId));
  CHECK(childExitCount == 1); // exit 恰好一次
  CHECK(rawRoot->childCount() == 0);
  CHECK(database.aliveCount() == baseline + 1); // 只剩 root
}

TEST_CASE("queueFree: deleting one child keeps parent and siblings intact", "[scene][queuefree][tree]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<QueueNode>("root");
  auto first = std::make_unique<QueueNode>("first");
  auto second = std::make_unique<QueueNode>("second");
  auto third = std::make_unique<QueueNode>("third");
  QueueNode* const rawRoot = root.get();
  QueueNode* const rawFirst = first.get();
  QueueNode* const rawSecond = second.get();
  QueueNode* const rawThird = third.get();
  const yr::core::ObjectID secondId = rawSecond->id();
  REQUIRE(rawRoot->addChild(std::move(first)));
  REQUIRE(rawRoot->addChild(std::move(second)));
  REQUIRE(rawRoot->addChild(std::move(third)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  rawSecond->queueFree();
  tree.flushDeletions();

  CHECK_FALSE(database.contains(secondId));
  CHECK(rawRoot->childCount() == 2);
  CHECK(rawRoot->childAt(0) == rawFirst); // 兄弟不受影响，顺序保持
  CHECK(rawRoot->childAt(1) == rawThird);
  CHECK(rawFirst->isInsideTree());
  CHECK(rawThird->isInsideTree());
  CHECK(database.aliveCount() == baseline + 3); // root + first + third
}

TEST_CASE("queueFree: queuing the root frees the whole tree; a second flush is a safe no-op",
          "[scene][queuefree][tree][root]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<QueueNode>("root");
  auto leaf = std::make_unique<QueueNode>("leaf");
  QueueNode* const rawRoot = root.get();
  const yr::core::ObjectID rootId = rawRoot->id();
  REQUIRE(rawRoot->addChild(std::move(leaf)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  rawRoot->queueFree();
  tree.flushDeletions();

  CHECK(tree.root() == nullptr);
  CHECK_FALSE(database.contains(rootId));
  CHECK(database.aliveCount() == baseline);
  CHECK(g_order == std::vector<std::string>{"leaf:exit", "root:exit"}); // 后序 exit

  // 已经是空树，再次 flush 安全 no-op。
  tree.flushDeletions();
  CHECK(tree.root() == nullptr);
  CHECK(database.aliveCount() == baseline);
}

TEST_CASE("queueFree: parent and child queued together free without double free, child exits first",
          "[scene][queuefree][tree][order]") {
  const auto run = [](bool parentFirst) {
    yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
    const std::size_t baseline = database.aliveCount();
    g_order.clear();

    auto grand = std::make_unique<QueueNode>("grand");
    auto parent = std::make_unique<QueueNode>("parent");
    auto child = std::make_unique<QueueNode>("child");
    QueueNode* const rawGrand = grand.get();
    QueueNode* const rawParent = parent.get();
    QueueNode* const rawChild = child.get();
    const yr::core::ObjectID parentId = rawParent->id();
    const yr::core::ObjectID childId = rawChild->id();
    REQUIRE(rawParent->addChild(std::move(child)));
    REQUIRE(rawGrand->addChild(std::move(parent)));

    yr::scene::SceneTree tree;
    REQUIRE(tree.setRoot(std::move(grand)));

    if (parentFirst) {
      rawParent->queueFree();
      rawChild->queueFree();
    } else {
      rawChild->queueFree();
      rawParent->queueFree();
    }

    tree.flushDeletions();

    // 父先删时子的 ID 解析失效被跳过；两种入队顺序都不得 double free（ASan 验证）。
    CHECK_FALSE(database.contains(parentId));
    CHECK_FALSE(database.contains(childId));
    CHECK(database.aliveCount() == baseline + 1);                            // 只剩 grand
    CHECK(g_order == std::vector<std::string>{"child:exit", "parent:exit"}); // 子先父后
  };

  SECTION("parent queued first") {
    run(true);
  }
  SECTION("child queued first") {
    run(false);
  }
}

TEST_CASE("queueFree: repeated calls on the same node are idempotent", "[scene][queuefree][idempotent]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<QueueNode>("root");
  auto child = std::make_unique<QueueNode>("child");
  QueueNode* const rawRoot = root.get();
  QueueNode* const rawChild = child.get();
  const yr::core::ObjectID childId = rawChild->id();
  REQUIRE(rawRoot->addChild(std::move(child)));

  int exitCount = 0;
  rawChild->onExitHook = [&](QueueNode&) { ++exitCount; };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  rawChild->queueFree();
  rawChild->queueFree();
  CHECK(rawChild->isQueuedForDeletion());
  CHECK(rawRoot->childCount() == 1); // 重复调用不改结构

  tree.flushDeletions();
  CHECK_FALSE(database.contains(childId));
  CHECK(exitCount == 1);

  // 队列已清空：再次 flush 不产生任何销毁。
  tree.flushDeletions();
  CHECK(exitCount == 1);
  CHECK(database.aliveCount() == baseline + 1);
}

TEST_CASE("queueFree: detach cancels deletion intent, reattach can queue again and frees exactly once",
          "[scene][queuefree][requeue]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<QueueNode>("root");
  auto child = std::make_unique<QueueNode>("child");
  QueueNode* const rawRoot = root.get();
  QueueNode* const rawChild = child.get();
  const yr::core::ObjectID childId = rawChild->id();
  REQUIRE(rawRoot->addChild(std::move(child)));

  int childExitCount = 0;
  rawChild->onExitHook = [&](QueueNode&) { ++childExitCount; };

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  REQUIRE(rawChild->readyCount == 1);

  rawChild->queueFree();
  CHECK(rawChild->isQueuedForDeletion());

  // 手动摘除 = 取消删除意图：标志复位、exit 一次、队列里的旧 ID 成为陈旧条目。
  std::unique_ptr<yr::scene::Node> detached = rawRoot->removeChild(rawChild);
  REQUIRE(detached.get() == rawChild);
  CHECK_FALSE(rawChild->isQueuedForDeletion());
  CHECK(childExitCount == 1);
  CHECK_FALSE(rawChild->isInsideTree());

  // 重新入树：只重新 enter（everReady 不重置），旧节点不重复 ready。
  REQUIRE(rawRoot->addChild(std::move(detached)));
  CHECK(rawChild->isInsideTree());
  CHECK(rawChild->readyCount == 1);

  // 复位后可以再次 queueFree，入队同一 ID。
  rawChild->queueFree();
  CHECK(rawChild->isQueuedForDeletion());

  // 队列里 now 有 child 的旧 + 新两个条目：先删一次，第二个条目因 ID 解析失效被跳过，
  // 不得 double free（ASan 验证），exit 恰好两次（摘除一次 + flush 一次）。
  tree.flushDeletions();
  CHECK_FALSE(database.contains(childId));
  CHECK(childExitCount == 2);
  CHECK(rawRoot->childCount() == 0);
  CHECK(database.aliveCount() == baseline + 1); // 只剩 root
}

TEST_CASE("queueFree: detach then reattach WITHOUT re-queueing is not freed by flush", "[scene][queuefree][requeue]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<QueueNode>("root");
  auto child = std::make_unique<QueueNode>("child");
  QueueNode* const rawRoot = root.get();
  QueueNode* const rawChild = child.get();
  const yr::core::ObjectID childId = rawChild->id();
  REQUIRE(rawRoot->addChild(std::move(child)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  rawChild->queueFree();
  std::unique_ptr<yr::scene::Node> detached = rawRoot->removeChild(rawChild);
  REQUIRE(detached.get() == rawChild);
  REQUIRE(rawRoot->addChild(std::move(detached))); // 复位后重新入树，但不再 queueFree

  // 陈旧条目必须只跳过，不能把已重新挂回树的节点删掉。
  tree.flushDeletions();
  CHECK(database.contains(childId));
  CHECK(rawChild->isInsideTree());
  CHECK(rawRoot->childCount() == 1);
  CHECK(rawRoot->childAt(0) == rawChild);
  CHECK_FALSE(rawChild->isQueuedForDeletion());
  CHECK(database.aliveCount() == baseline + 2);
}

TEST_CASE("queueFree: a request made during flush is deferred to the next flush", "[scene][queuefree][snapshot]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<QueueNode>("root");
  auto first = std::make_unique<QueueNode>("first");
  auto second = std::make_unique<QueueNode>("second");
  QueueNode* const rawRoot = root.get();
  QueueNode* const rawFirst = first.get();
  QueueNode* const rawSecond = second.get();
  const yr::core::ObjectID firstId = rawFirst->id();
  const yr::core::ObjectID secondId = rawSecond->id();
  REQUIRE(rawRoot->addChild(std::move(first)));
  REQUIRE(rawRoot->addChild(std::move(second)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  // first 退出时再 queueFree(second)：快照语义下 second 留到下一次 flush。
  rawFirst->onExitHook = [&](QueueNode&) { rawSecond->queueFree(); };
  rawFirst->queueFree();

  tree.flushDeletions();
  CHECK_FALSE(database.contains(firstId));
  CHECK(database.contains(secondId)); // 本批不处理 flush 期间的新请求
  CHECK(rawSecond->isInsideTree());
  CHECK(rawSecond->isQueuedForDeletion());
  CHECK(rawRoot->childCount() == 1);

  tree.flushDeletions();
  CHECK_FALSE(database.contains(secondId));
  CHECK(rawRoot->childCount() == 0);
  CHECK(database.aliveCount() == baseline + 1);
}

TEST_CASE("queueFree: a node outside any tree asserts and is not consumed", "[scene][queuefree][precondition]") {
  ScopedContinueAssertHandler handler;

  auto orphan = std::make_unique<QueueNode>("orphan");
  QueueNode* const raw = orphan.get();
  const yr::core::ObjectID id = raw->id();

  CHECK_FALSE(raw->isInsideTree());
  raw->queueFree();
  raw->queueFree(); // 重复调用同样安全

  // 不入队、不置标记、不销毁：所有权仍在外部。
  CHECK_FALSE(raw->isQueuedForDeletion());
  CHECK(orphan != nullptr);
  CHECK(raw->id() == id);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("flushDeletions: a queued node detached before flush has its flag reset and is skipped, not freed",
          "[scene][queuefree][orphan][precondition]") {
  ScopedContinueAssertHandler handler;

  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<QueueNode>("root");
  auto child = std::make_unique<QueueNode>("child");
  QueueNode* const rawRoot = root.get();
  QueueNode* const rawChild = child.get();
  const yr::core::ObjectID rootId = rawRoot->id();
  const yr::core::ObjectID childId = rawChild->id();
  REQUIRE(rawRoot->addChild(std::move(child)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  REQUIRE(tree.root() == rawRoot);

  rawChild->queueFree();
  // flush 前把已排队的 child 手动摘下：手动摘除 = 取消删除意图，标志复位，
  // 所有权回到外部。队列里的 ID 成为陈旧条目。
  std::unique_ptr<yr::scene::Node> orphan = rawRoot->removeChild(rawChild);
  REQUIRE(orphan.get() == rawChild);

  tree.flushDeletions();

  // 标志已复位 → flush 在 !isQueuedForDeletion() 处静默跳过（不进入 orphan 分支，
  // 因此不应出现孤儿断言）；child 绝不删除，树的根不受影响。
  CHECK(database.contains(childId)); // child 仍存活
  CHECK(orphan != nullptr);
  CHECK_FALSE(rawChild->isQueuedForDeletion());
  CHECK(tree.root() == rawRoot); // 根没有被误删
  CHECK(database.contains(rootId));
  CHECK(database.aliveCount() == baseline + 2);
  CHECK(g_assertCount == 0);
}

TEST_CASE("flushDeletions: recursive flush from an exit callback is rejected and deferred",
          "[scene][queuefree][reentrancy]") {
  ScopedContinueAssertHandler handler;

  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();
  g_order.clear();

  auto root = std::make_unique<QueueNode>("root");
  auto first = std::make_unique<QueueNode>("first");
  auto second = std::make_unique<QueueNode>("second");
  QueueNode* const rawRoot = root.get();
  QueueNode* const rawFirst = first.get();
  QueueNode* const rawSecond = second.get();
  const yr::core::ObjectID firstId = rawFirst->id();
  const yr::core::ObjectID secondId = rawSecond->id();
  REQUIRE(rawRoot->addChild(std::move(first)));
  REQUIRE(rawRoot->addChild(std::move(second)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  // first 退出时 queueFree(second) 并直接重入 flush：重入被拒绝，second 留到下一次 flush。
  rawFirst->onExitHook = [&](QueueNode&) {
    rawSecond->queueFree();
    tree.flushDeletions();
  };
  rawFirst->queueFree();

  tree.flushDeletions();
  CHECK_FALSE(database.contains(firstId));
  CHECK(database.contains(secondId)); // 重入 flush 未处理 second
  CHECK(rawSecond->isInsideTree());
  CHECK(rawSecond->isQueuedForDeletion());
  CHECK(rawRoot->childCount() == 1);

  tree.flushDeletions();
  CHECK_FALSE(database.contains(secondId));
  CHECK(rawRoot->childCount() == 0);
  CHECK(database.aliveCount() == baseline + 1);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}