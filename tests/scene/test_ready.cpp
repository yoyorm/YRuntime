#include <catch2/catch_test_macros.hpp>

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

  // 记录 enter / ready / exit 顺序与次数，并快照回调执行时的树内状态。
  class RecordingNode : public yr::scene::Node {
  public:
    explicit RecordingNode(std::string label) : label_(std::move(label)) {}

    [[nodiscard]] const std::string& label() const noexcept { return label_; }

    int enterCount = 0;
    int readyCount = 0;
    int exitCount = 0;
    TreeState enterState;
    TreeState readyState;
    TreeState exitState;
    // 本节点 onReady 时，所有直接子节点是否已经 ready（父后于子 ready 的证明）。
    bool allDirectChildrenReady = true;

  protected:
    void onEnterTree() override {
      ++enterCount;
      enterState = capture();
      g_order.push_back(label_ + ":enter");
    }

    void onReady() override {
      ++readyCount;
      readyState = capture();
      allDirectChildrenReady = true;
      for (std::size_t i = 0; i < childCount(); ++i) {
        const auto* child = dynamic_cast<const RecordingNode*>(childAt(i));
        if (child != nullptr && child->readyCount == 0) {
          allDirectChildrenReady = false;
        }
      }
      g_order.push_back(label_ + ":ready");
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

} // namespace

TEST_CASE("ready: offline construction emits neither enter nor ready", "[scene][ready]") {
  g_order.clear();

  auto root = std::make_unique<RecordingNode>("root");
  auto child = std::make_unique<RecordingNode>("child");
  RecordingNode* rawRoot = root.get();
  RecordingNode* rawChild = child.get();
  REQUIRE(rawRoot->addChild(std::move(child)));

  CHECK(g_order.empty());
  CHECK(rawRoot->enterCount == 0);
  CHECK(rawRoot->readyCount == 0);
  CHECK(rawChild->enterCount == 0);
  CHECK(rawChild->readyCount == 0);
  CHECK_FALSE(rawRoot->isInsideTree());
  CHECK_FALSE(rawChild->isInsideTree());
}

TEST_CASE("ready: first setRoot runs all enter pre-order then ready post-order once", "[scene][ready]") {
  g_order.clear();

  auto root = std::make_unique<RecordingNode>("root");
  auto middle = std::make_unique<RecordingNode>("middle");
  auto leaf = std::make_unique<RecordingNode>("leaf");
  RecordingNode* rawRoot = root.get();
  RecordingNode* rawMiddle = middle.get();
  RecordingNode* rawLeaf = leaf.get();
  REQUIRE(rawMiddle->addChild(std::move(leaf)));
  REQUIRE(rawRoot->addChild(std::move(middle)));

  // 离线构树不触发任何回调。
  CHECK(g_order.empty());
  REQUIRE(rawRoot->readyCount == 0);

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));

  CHECK(g_order == std::vector<std::string>{"root:enter", "middle:enter", "leaf:enter", "leaf:ready", "middle:ready",
                                            "root:ready"});
  CHECK(rawRoot->enterCount == 1);
  CHECK(rawRoot->readyCount == 1);
  CHECK(rawMiddle->readyCount == 1);
  CHECK(rawLeaf->readyCount == 1);

  // ready 回调中：在树内、tree()/parent 正确。
  CHECK(rawRoot->readyState.inside);
  CHECK(rawRoot->readyState.tree == &tree);
  CHECK(rawRoot->readyState.parent == nullptr);
  CHECK(rawMiddle->readyState.inside);
  CHECK(rawMiddle->readyState.tree == &tree);
  CHECK(rawMiddle->readyState.parent == rawRoot);
  CHECK(rawLeaf->readyState.inside);
  CHECK(rawLeaf->readyState.tree == &tree);
  CHECK(rawLeaf->readyState.parent == rawMiddle);

  // 父 ready 时，直接子节点已经 ready（后序）。
  CHECK(rawRoot->allDirectChildrenReady);
  CHECK(rawMiddle->allDirectChildrenReady);
}

TEST_CASE("ready: detach and reattach to a ready parent emits exit+enter but not ready again", "[scene][ready]") {
  g_order.clear();

  auto root = std::make_unique<RecordingNode>("root");
  auto middle = std::make_unique<RecordingNode>("middle");
  auto leaf = std::make_unique<RecordingNode>("leaf");
  RecordingNode* rawRoot = root.get();
  RecordingNode* rawMiddle = middle.get();
  RecordingNode* rawLeaf = leaf.get();
  REQUIRE(rawMiddle->addChild(std::move(leaf)));
  REQUIRE(rawRoot->addChild(std::move(middle)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  REQUIRE(rawMiddle->readyCount == 1);
  g_order.clear();

  std::unique_ptr<yr::scene::Node> detached = rawMiddle->detach();
  REQUIRE(detached.get() == rawMiddle);
  CHECK(g_order == std::vector<std::string>{"leaf:exit", "middle:exit"});

  g_order.clear();
  REQUIRE(rawRoot->addChild(std::move(detached)));
  // 只有 enter，没有 ready：旧节点不重复 ready。
  CHECK(g_order == std::vector<std::string>{"middle:enter", "leaf:enter"});
  CHECK(rawMiddle->readyCount == 1);
  CHECK(rawLeaf->readyCount == 1);
  CHECK(rawMiddle->enterCount == 2);
  CHECK(rawMiddle->exitCount == 1);
}

TEST_CASE("ready: clearRoot then setRoot same tree re-enters but does not ready again", "[scene][ready]") {
  g_order.clear();

  auto root = std::make_unique<RecordingNode>("root");
  auto leaf = std::make_unique<RecordingNode>("leaf");
  RecordingNode* rawRoot = root.get();
  RecordingNode* rawLeaf = leaf.get();
  REQUIRE(rawRoot->addChild(std::move(leaf)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  REQUIRE(rawRoot->readyCount == 1);
  g_order.clear();

  std::unique_ptr<yr::scene::Node> returned = tree.clearRoot();
  REQUIRE(returned.get() == rawRoot);
  CHECK(g_order == std::vector<std::string>{"leaf:exit", "root:exit"});

  g_order.clear();
  REQUIRE(tree.setRoot(std::move(returned)));
  CHECK(g_order == std::vector<std::string>{"root:enter", "leaf:enter"});
  CHECK(rawRoot->readyCount == 1);
  CHECK(rawLeaf->readyCount == 1);
  CHECK(rawRoot->enterCount == 2);
  CHECK(rawRoot->exitCount == 1);
}

TEST_CASE("ready: adding a new subtree to a ready parent enters then readies only the new subtree", "[scene][ready]") {
  g_order.clear();

  auto root = std::make_unique<RecordingNode>("root");
  auto existing = std::make_unique<RecordingNode>("existing");
  RecordingNode* rawRoot = root.get();
  RecordingNode* rawExisting = existing.get();
  REQUIRE(rawRoot->addChild(std::move(existing)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  REQUIRE(rawRoot->readyCount == 1);
  REQUIRE(rawExisting->readyCount == 1);
  g_order.clear();

  auto sub = std::make_unique<RecordingNode>("sub");
  auto subLeaf = std::make_unique<RecordingNode>("subLeaf");
  RecordingNode* rawSub = sub.get();
  RecordingNode* rawSubLeaf = subLeaf.get();
  REQUIRE(rawSub->addChild(std::move(subLeaf)));

  REQUIRE(rawRoot->addChild(std::move(sub)));

  // enter 前序，然后 ready 后序，且只针对新子树。
  CHECK(g_order == std::vector<std::string>{"sub:enter", "subLeaf:enter", "subLeaf:ready", "sub:ready"});
  CHECK(rawRoot->readyCount == 1); // 旧节点不重复 ready
  CHECK(rawExisting->readyCount == 1);
  CHECK(rawSub->readyCount == 1);
  CHECK(rawSubLeaf->readyCount == 1);
  CHECK(rawSub->tree() == &tree);
  CHECK(rawSubLeaf->tree() == &tree);
  CHECK(rawSub->parent() == rawRoot);
}

TEST_CASE("ready: reattaching an already-ready subtree with a brand-new offline child readies only the new child",
          "[scene][ready]") {
  g_order.clear();

  auto root = std::make_unique<RecordingNode>("root");
  auto middle = std::make_unique<RecordingNode>("middle");
  auto leaf = std::make_unique<RecordingNode>("leaf");
  RecordingNode* rawRoot = root.get();
  RecordingNode* rawMiddle = middle.get();
  RecordingNode* rawLeaf = leaf.get();
  REQUIRE(rawMiddle->addChild(std::move(leaf)));
  REQUIRE(rawRoot->addChild(std::move(middle)));

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  REQUIRE(rawMiddle->readyCount == 1);
  REQUIRE(rawLeaf->readyCount == 1);

  // 离线摘出整棵 middle 子树（exit），旧节点的 everReady 不重置。
  std::unique_ptr<yr::scene::Node> detached = rawMiddle->detach();
  REQUIRE(detached.get() == rawMiddle);
  g_order.clear();

  // 离线新增一个全新 child：不进树、不 ready。
  auto extra = std::make_unique<RecordingNode>("extra");
  RecordingNode* rawExtra = extra.get();
  REQUIRE(rawMiddle->addChild(std::move(extra)));
  CHECK(rawExtra->enterCount == 0);
  CHECK(rawExtra->readyCount == 0);
  CHECK(g_order.empty());

  // 重新挂到仍然 ready 的 root：旧节点只 enter，新 child 才 ready。
  REQUIRE(rawRoot->addChild(std::move(detached)));

  CHECK(g_order == std::vector<std::string>{"middle:enter", "leaf:enter", "extra:enter", "extra:ready"});
  CHECK(rawMiddle->readyCount == 1); // 旧节点不重复 ready
  CHECK(rawLeaf->readyCount == 1);
  CHECK(rawExtra->readyCount == 1);
  CHECK(rawExtra->tree() == &tree);
  CHECK(rawExtra->enterCount == 1);
}