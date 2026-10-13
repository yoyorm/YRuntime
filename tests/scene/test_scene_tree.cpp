#include <catch2/catch_test_macros.hpp>

#include <yr/core/assert.h>
#include <yr/object/object_db.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

// SceneTree 是整棵树的唯一所有者，root_ 是 unique_ptr，因此禁止复制与移动。
static_assert(!std::is_copy_constructible_v<yr::scene::SceneTree>);
static_assert(!std::is_copy_assignable_v<yr::scene::SceneTree>);
static_assert(!std::is_move_constructible_v<yr::scene::SceneTree>);
static_assert(!std::is_move_assignable_v<yr::scene::SceneTree>);

// 访问器：const + noexcept。
static_assert(noexcept(std::declval<const yr::scene::SceneTree&>().root()));
static_assert(noexcept(std::declval<yr::scene::SceneTree&>().clearRoot()));

namespace {

  int g_assertCount = 0;

  yr::core::AssertAction continueAssertHandler(const yr::core::AssertInfo& /*info*/) noexcept {
    ++g_assertCount;
    return yr::core::AssertAction::kContinue;
  }

  // 把全局 assert handler 换成“记录并继续”，析构时恢复默认。
  // 用它验证：即使断言被降级为 kContinue（或 Release 下宏为空），
  // 非法输入也只返回失败，不会消费参数或破坏树结构。
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

} // namespace

TEST_CASE("SceneTree: default construction yields an empty tree", "[scene][scene_tree][ownership]") {
  yr::scene::SceneTree tree;

  CHECK(tree.root() == nullptr);
  CHECK(tree.clearRoot() == nullptr); // 空树交还 root 安全返回 nullptr
}

TEST_CASE("SceneTree: setRoot consumes ownership on success", "[scene][scene_tree][ownership]") {
  yr::scene::SceneTree tree;
  auto root = std::make_unique<yr::scene::Node>();

  yr::scene::Node* const raw = root.get();

  REQUIRE(tree.setRoot(std::move(root)));
  CHECK(root == nullptr); // 所有权已移入 tree
  CHECK(tree.root() == raw);
  CHECK(raw->parent() == nullptr); // root 自身无父
}

TEST_CASE("SceneTree: setRoot rejects a null root safely", "[scene][scene_tree][precondition]") {
  ScopedContinueAssertHandler guard;

  yr::scene::SceneTree tree;
  std::unique_ptr<yr::scene::Node> nullRoot;

  CHECK_FALSE(tree.setRoot(std::move(nullRoot)));
  CHECK(tree.root() == nullptr);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("SceneTree: second setRoot fails and does not consume the new root", "[scene][scene_tree][precondition]") {
  ScopedContinueAssertHandler guard;

  yr::scene::SceneTree tree;
  auto first = std::make_unique<yr::scene::Node>();
  auto second = std::make_unique<yr::scene::Node>();

  yr::scene::Node* const rawFirst = first.get();
  yr::scene::Node* const rawSecond = second.get();

  REQUIRE(tree.setRoot(std::move(first)));
  REQUIRE(first == nullptr);

  CHECK_FALSE(tree.setRoot(std::move(second)));

  // 失败不消费参数：second 仍然有效且仍游离。
  REQUIRE(second != nullptr);
  CHECK(second.get() == rawSecond);
  CHECK(second->parent() == nullptr);
  CHECK(tree.root() == rawFirst); // 原 root 未被替换

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("SceneTree: clearRoot hands back the same object and empties the tree", "[scene][scene_tree][ownership]") {
  yr::scene::SceneTree tree;
  auto root = std::make_unique<yr::scene::Node>();

  yr::scene::Node* const raw = root.get();
  REQUIRE(tree.setRoot(std::move(root)));

  std::unique_ptr<yr::scene::Node> returned = tree.clearRoot();
  REQUIRE(returned.get() == raw);
  CHECK(tree.root() == nullptr);

  // 再次 clearRoot 应安全返回 nullptr。
  CHECK(tree.clearRoot() == nullptr);
}

TEST_CASE("SceneTree: destroying the tree cascades to root descendants and ObjectDB unregisters them",
          "[scene][scene_tree][ownership]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount(); // 不假设全局一定是 0

  yr::core::ObjectID leafId;
  {
    yr::scene::SceneTree tree;
    auto root = std::make_unique<yr::scene::Node>();
    auto middle = std::make_unique<yr::scene::Node>();
    auto leaf = std::make_unique<yr::scene::Node>();

    leafId = leaf->id();
    REQUIRE(middle->addChild(std::move(leaf)));
    REQUIRE(root->addChild(std::move(middle)));
    REQUIRE(tree.setRoot(std::move(root)));

    CHECK(database.aliveCount() == baseline + 3);
  }

  CHECK(database.aliveCount() == baseline);
  CHECK_FALSE(database.contains(leafId));
}