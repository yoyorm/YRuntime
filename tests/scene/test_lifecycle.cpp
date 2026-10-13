#include <catch2/catch_test_macros.hpp>

#include <yr/object/object_db.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

  // 每个 TEST_CASE 开头 clear()。单线程，仅用于断言回调顺序。
  std::vector<std::string> g_order;

  struct TreeState {
    bool inside = false;
    yr::scene::SceneTree* tree = nullptr;
    yr::scene::Node* parent = nullptr;
  };

  // 记录进入 / 离开顺序，并快照回调执行时的树内状态。
  class RecordingNode : public yr::scene::Node {
  public:
    explicit RecordingNode(std::string label) : label_(std::move(label)) {}

    [[nodiscard]] const std::string& label() const noexcept { return label_; }

    int enterCount = 0;
    int exitCount = 0;
    TreeState enterState;
    TreeState exitState;

  protected:
    void onEnterTree() override {
      ++enterCount;
      enterState = capture();
      g_order.push_back(label_ + ":enter");
    }

    void onExitTree() override {
      ++exitCount;
      exitState = capture();
      g_order.push_back(label_ + ":exit");
    }

  private:
    [[nodiscard]] TreeState capture() const noexcept { return TreeState{isInsideTree(), tree(), parent()}; }

    std::string label_;
  };

  // 三层树：root -> middle -> leaf（离线，未入树）。
  struct ThreeLayer {
    std::unique_ptr<RecordingNode> root;
    std::unique_ptr<RecordingNode> middle;
    std::unique_ptr<RecordingNode> leaf;
    RecordingNode* rawRoot = nullptr;
    RecordingNode* rawMiddle = nullptr;
    RecordingNode* rawLeaf = nullptr;

    ThreeLayer()
        : root(std::make_unique<RecordingNode>("root")), middle(std::make_unique<RecordingNode>("middle")),
          leaf(std::make_unique<RecordingNode>("leaf")) {
      rawRoot = root.get();
      rawMiddle = middle.get();
      rawLeaf = leaf.get();
      REQUIRE(rawMiddle->addChild(std::move(leaf)));
      REQUIRE(rawRoot->addChild(std::move(middle)));
    }
  };

} // namespace

TEST_CASE("lifecycle: offline construction emits nothing; setRoot enters pre-order", "[scene][lifecycle][enter]") {
  g_order.clear();
  ThreeLayer t;

  // 离线构树不触发任何回调，也不在树内。
  CHECK(g_order.empty());
  CHECK_FALSE(t.rawRoot->isInsideTree());
  CHECK(t.rawRoot->tree() == nullptr);

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(t.root)));

  CHECK(g_order == std::vector<std::string>{"root:enter", "middle:enter", "leaf:enter"});
  CHECK(t.rawRoot->enterCount == 1);
  CHECK(t.rawMiddle->enterCount == 1);
  CHECK(t.rawLeaf->enterCount == 1);

  // enter 回调中：已在树内、tree() 正确、父子关系已建立。
  CHECK(t.rawRoot->enterState.inside);
  CHECK(t.rawRoot->enterState.tree == &tree);
  CHECK(t.rawRoot->enterState.parent == nullptr);
  CHECK(t.rawMiddle->enterState.inside);
  CHECK(t.rawMiddle->enterState.tree == &tree);
  CHECK(t.rawMiddle->enterState.parent == t.rawRoot);
  CHECK(t.rawLeaf->enterState.inside);
  CHECK(t.rawLeaf->enterState.tree == &tree);
  CHECK(t.rawLeaf->enterState.parent == t.rawMiddle);
}

TEST_CASE("lifecycle: clearRoot exits post-order with children reversed", "[scene][lifecycle][exit]") {
  g_order.clear();
  ThreeLayer t;
  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(t.root)));
  g_order.clear();

  std::unique_ptr<yr::scene::Node> returned = tree.clearRoot();
  REQUIRE(returned.get() == t.rawRoot);

  CHECK(g_order == std::vector<std::string>{"leaf:exit", "middle:exit", "root:exit"});

  // 回调执行期间仍在树内，且 parent 关系完整。
  CHECK(t.rawLeaf->exitState.inside);
  CHECK(t.rawLeaf->exitState.tree == &tree);
  CHECK(t.rawLeaf->exitState.parent == t.rawMiddle);
  CHECK(t.rawMiddle->exitState.inside);
  CHECK(t.rawMiddle->exitState.parent == t.rawRoot);
  CHECK(t.rawRoot->exitState.inside);
  CHECK(t.rawRoot->exitState.tree == &tree);

  // 完成后整棵返回子树都不在树内，但 parent 关系保留。
  CHECK_FALSE(t.rawRoot->isInsideTree());
  CHECK(t.rawRoot->tree() == nullptr);
  CHECK(t.rawMiddle->tree() == nullptr);
  CHECK(t.rawLeaf->tree() == nullptr);
  CHECK(t.rawRoot->parent() == nullptr);
  CHECK(t.rawLeaf->parent() == t.rawMiddle);

  // 空树路径：再次 clearRoot 安全返回 nullptr。
  CHECK(tree.clearRoot() == nullptr);
}

TEST_CASE("lifecycle: sibling exit order is reverse of insertion", "[scene][lifecycle][exit]") {
  g_order.clear();
  auto root = std::make_unique<RecordingNode>("root");
  auto first = std::make_unique<RecordingNode>("first");
  auto second = std::make_unique<RecordingNode>("second");
  RecordingNode* rawRoot = root.get();

  REQUIRE(rawRoot->addChild(std::move(first)));
  REQUIRE(rawRoot->addChild(std::move(second)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  CHECK(g_order == std::vector<std::string>{"root:enter", "first:enter", "second:enter"});

  g_order.clear();
  std::unique_ptr<yr::scene::Node> returned = tree.clearRoot();
  CHECK(returned != nullptr);
  CHECK(g_order == std::vector<std::string>{"second:exit", "first:exit", "root:exit"});
}

TEST_CASE("lifecycle: adding a subtree to an in-tree parent enters only the new subtree", "[scene][lifecycle][enter]") {
  g_order.clear();
  auto root = std::make_unique<RecordingNode>("root");
  auto existing = std::make_unique<RecordingNode>("existing");
  RecordingNode* rawRoot = root.get();
  RecordingNode* rawExisting = existing.get();
  REQUIRE(rawRoot->addChild(std::move(existing)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  REQUIRE(rawExisting->enterCount == 1);
  g_order.clear();

  auto sub = std::make_unique<RecordingNode>("sub");
  auto subLeaf = std::make_unique<RecordingNode>("subLeaf");
  RecordingNode* rawSub = sub.get();
  RecordingNode* rawSubLeaf = subLeaf.get();
  REQUIRE(rawSub->addChild(std::move(subLeaf)));

  REQUIRE(rawRoot->addChild(std::move(sub)));

  CHECK(g_order == std::vector<std::string>{"sub:enter", "subLeaf:enter"});
  CHECK(rawRoot->enterCount == 1); // 旧节点不重复 enter
  CHECK(rawExisting->enterCount == 1);
  CHECK(rawSub->tree() == &tree);
  CHECK(rawSubLeaf->tree() == &tree);
  CHECK(rawSub->parent() == rawRoot);
}

TEST_CASE("lifecycle: removeChild exits only the removed subtree with parent intact", "[scene][lifecycle][exit]") {
  g_order.clear();
  ThreeLayer t;
  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(t.root)));
  g_order.clear();

  std::unique_ptr<yr::scene::Node> removed = t.rawRoot->removeChild(t.rawMiddle);
  REQUIRE(removed.get() == t.rawMiddle);

  CHECK(g_order == std::vector<std::string>{"leaf:exit", "middle:exit"});
  CHECK(t.rawMiddle->exitState.inside);
  CHECK(t.rawMiddle->exitState.parent == t.rawRoot); // exit 回调中 parent 仍有效
  CHECK(t.rawLeaf->exitState.parent == t.rawMiddle);
  CHECK(t.rawRoot->exitCount == 0); // 留在树内的 root 不受影响
  CHECK(t.rawMiddle->exitCount == 1);
  CHECK(t.rawLeaf->exitCount == 1);

  // 完成后：返回的子树已离树，parent 清空、root 子列表收缩。
  CHECK_FALSE(t.rawMiddle->isInsideTree());
  CHECK(t.rawRoot->childCount() == 0);
  CHECK(removed->parent() == nullptr);
}

TEST_CASE("lifecycle: detach is equivalent to removeChild for lifecycle", "[scene][lifecycle][exit]") {
  g_order.clear();
  ThreeLayer t;
  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(t.root)));
  g_order.clear();

  std::unique_ptr<yr::scene::Node> detached = t.rawMiddle->detach();
  REQUIRE(detached.get() == t.rawMiddle);

  CHECK(g_order == std::vector<std::string>{"leaf:exit", "middle:exit"});
  CHECK(t.rawRoot->childCount() == 0);
  CHECK_FALSE(t.rawMiddle->isInsideTree());
}

TEST_CASE("lifecycle: remove then add across trees exits old tree then enters new tree", "[scene][lifecycle][enter]") {
  g_order.clear();

  auto rootA = std::make_unique<RecordingNode>("rootA");
  auto child = std::make_unique<RecordingNode>("child");
  RecordingNode* rawRootA = rootA.get();
  RecordingNode* rawChild = child.get();
  REQUIRE(rawRootA->addChild(std::move(child)));

  auto rootB = std::make_unique<RecordingNode>("rootB");
  RecordingNode* rawRootB = rootB.get();

  yr::scene::SceneTree treeA;
  yr::scene::SceneTree treeB;
  REQUIRE(treeA.setRoot(std::move(rootA)));
  REQUIRE(treeB.setRoot(std::move(rootB)));
  REQUIRE(rawChild->tree() == &treeA);
  g_order.clear();

  std::unique_ptr<yr::scene::Node> moved = rawRootA->removeChild(rawChild);
  CHECK(g_order == std::vector<std::string>{"child:exit"});
  CHECK_FALSE(rawChild->isInsideTree());

  g_order.clear();
  REQUIRE(rawRootB->addChild(std::move(moved)));
  CHECK(g_order == std::vector<std::string>{"child:enter"});
  CHECK(rawChild->tree() == &treeB);
  CHECK(rawChild->parent() == rawRootB);
}

TEST_CASE("lifecycle: SceneTree destructor exits once then destroys via unique_ptr",
          "[scene][lifecycle][exit][ownership]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount();

  g_order.clear();
  {
    auto root = std::make_unique<RecordingNode>("root");
    auto leaf = std::make_unique<RecordingNode>("leaf");
    RecordingNode* rawRoot = root.get();
    REQUIRE(rawRoot->addChild(std::move(leaf)));

    yr::scene::SceneTree tree;
    REQUIRE(tree.setRoot(std::move(root)));
    REQUIRE(database.aliveCount() == baseline + 2);
    g_order.clear();
  } // SceneTree 析构：exit 一次，然后 unique_ptr 级联销毁

  CHECK(g_order == std::vector<std::string>{"leaf:exit", "root:exit"});
  CHECK(database.aliveCount() == baseline);
}