#include <catch2/catch_test_macros.hpp>

#include <yr/core/assert.h>
#include <yr/object/object_db.h>
#include <yr/scene/node.h>

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

// Node 继承 Object（ObjectID 与地址绑定），因此禁止复制与移动。
static_assert(!std::is_copy_constructible_v<yr::scene::Node>);
static_assert(!std::is_copy_assignable_v<yr::scene::Node>);
static_assert(!std::is_move_constructible_v<yr::scene::Node>);
static_assert(!std::is_move_assignable_v<yr::scene::Node>);
static_assert(std::has_virtual_destructor_v<yr::scene::Node>);

// 访问器：const + noexcept。
static_assert(noexcept(std::declval<const yr::scene::Node&>().parent()));
static_assert(noexcept(std::declval<const yr::scene::Node&>().childCount()));
static_assert(noexcept(std::declval<const yr::scene::Node&>().childAt(0)));

namespace {

  int g_assertCount = 0;

  yr::core::AssertAction continueAssertHandler(const yr::core::AssertInfo& /*info*/) noexcept {
    ++g_assertCount;
    return yr::core::AssertAction::kContinue;
  }

  // 把全局 assert handler 换成“记录并继续”，析构时恢复默认。
  // 用它验证：即使断言被降级为 kContinue（或 Release 下宏为空），
  // 非法输入也只返回失败，不会继续解引用或破坏树结构。
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

TEST_CASE("Node: addChild transfers ownership and preserves insertion order", "[scene][node][ownership]") {
  auto parent = std::make_unique<yr::scene::Node>();
  auto first = std::make_unique<yr::scene::Node>();
  auto second = std::make_unique<yr::scene::Node>();

  yr::scene::Node* const rawFirst = first.get();
  yr::scene::Node* const rawSecond = second.get();

  REQUIRE(parent->addChild(std::move(first)));
  CHECK(first == nullptr); // 所有权已移入 parent
  REQUIRE(parent->addChild(std::move(second)));
  CHECK(second == nullptr);

  CHECK(parent->parent() == nullptr);
  CHECK(parent->childCount() == 2);
  CHECK(parent->childAt(0) == rawFirst);
  CHECK(parent->childAt(1) == rawSecond);
  CHECK(rawFirst->parent() == parent.get());
  CHECK(rawSecond->parent() == parent.get());
}

TEST_CASE("Node: removeChild hands back ownership and clears the parent link", "[scene][node][ownership]") {
  auto parent = std::make_unique<yr::scene::Node>();
  auto other = std::make_unique<yr::scene::Node>();
  auto first = std::make_unique<yr::scene::Node>();
  auto second = std::make_unique<yr::scene::Node>();

  yr::scene::Node* const rawFirst = first.get();
  yr::scene::Node* const rawSecond = second.get();

  REQUIRE(parent->addChild(std::move(first)));
  REQUIRE(parent->addChild(std::move(second)));

  std::unique_ptr<yr::scene::Node> removed = parent->removeChild(rawFirst);
  REQUIRE(removed.get() == rawFirst);
  CHECK(removed->parent() == nullptr);
  CHECK(parent->childCount() == 1);
  CHECK(parent->childAt(0) == rawSecond);

  // 摘下后可以挂到另一个父节点（合法 reparent 路径）。
  REQUIRE(other->addChild(std::move(removed)));
  CHECK(removed == nullptr);
  CHECK(rawFirst->parent() == other.get());
  CHECK(other->childCount() == 1);
}

TEST_CASE("Node: detach removes self from parent and returns ownership", "[scene][node][ownership]") {
  auto parent = std::make_unique<yr::scene::Node>();
  auto child = std::make_unique<yr::scene::Node>();

  yr::scene::Node* const raw = child.get();
  REQUIRE(parent->addChild(std::move(child)));

  std::unique_ptr<yr::scene::Node> detached = raw->detach();
  REQUIRE(detached.get() == raw);
  CHECK(detached->parent() == nullptr);
  CHECK(parent->childCount() == 0);

  CHECK(raw->detach() == nullptr); // 无父时返回 nullptr
}

TEST_CASE("Node: destroying parent cascades to descendants and ObjectDB unregisters them", "[scene][node][ownership]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t baseline = database.aliveCount(); // 不假设全局一定是 0

  yr::core::ObjectID leafId;
  {
    auto root = std::make_unique<yr::scene::Node>();
    auto middle = std::make_unique<yr::scene::Node>();
    auto leaf = std::make_unique<yr::scene::Node>();

    leafId = leaf->id();
    REQUIRE(middle->addChild(std::move(leaf)));
    REQUIRE(root->addChild(std::move(middle)));

    CHECK(database.aliveCount() == baseline + 3);
  }

  CHECK(database.aliveCount() == baseline);
  CHECK_FALSE(database.contains(leafId));
}

TEST_CASE("Node: addChild rejects a null child safely", "[scene][node][precondition]") {
  ScopedContinueAssertHandler guard;

  auto parent = std::make_unique<yr::scene::Node>();
  std::unique_ptr<yr::scene::Node> nullChild;

  CHECK_FALSE(parent->addChild(std::move(nullChild)));
  CHECK(parent->childCount() == 0);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("Node: addChild rejects self safely", "[scene][node][precondition]") {
  ScopedContinueAssertHandler guard;

  auto self = std::make_unique<yr::scene::Node>();
  yr::scene::Node* const raw = self.get();

  CHECK_FALSE(self->addChild(std::move(self)));

  // 失败不消费参数：self 仍然有效且仍是根。
  REQUIRE(self != nullptr);
  CHECK(self.get() == raw);
  CHECK(self->childCount() == 0);
  CHECK(self->parent() == nullptr);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("Node: addChild rejects a child that already has a parent", "[scene][node][precondition]") {
  ScopedContinueAssertHandler guard;

  auto firstParent = std::make_unique<yr::scene::Node>();
  auto secondParent = std::make_unique<yr::scene::Node>();
  auto child = std::make_unique<yr::scene::Node>();

  yr::scene::Node* const raw = child.get();
  REQUIRE(firstParent->addChild(std::move(child)));
  REQUIRE(child == nullptr);
  REQUIRE(raw->parent() == firstParent.get());

  // 合法所有权下调用者不可能同时持有已被 firstParent 拥有节点的 unique_ptr，
  // 这正是 unique_ptr 要杜绝的“多父”。为了打到防御性前置条件，这里临时构造
  // 一个别名 unique_ptr：失败路径不消费参数，我们在别名析构前 release()，
  // 保证它绝不 delete（否则会与 firstParent 形成 double free）。
  std::unique_ptr<yr::scene::Node> alias(raw);
  CHECK_FALSE(secondParent->addChild(std::move(alias)));
  alias.release();

  CHECK(raw->parent() == firstParent.get());
  CHECK(secondParent->childCount() == 0);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("Node: addChild rejects creating a cycle", "[scene][node][precondition]") {
  ScopedContinueAssertHandler guard;

  auto root = std::make_unique<yr::scene::Node>();
  auto middle = std::make_unique<yr::scene::Node>();
  auto leaf = std::make_unique<yr::scene::Node>();

  yr::scene::Node* const rawMiddle = middle.get();
  yr::scene::Node* const rawLeaf = leaf.get();

  REQUIRE(root->addChild(std::move(middle)));
  REQUIRE(rawMiddle->addChild(std::move(leaf)));

  // root 是 leaf 的祖先，把 root 挂到 leaf 下面会成环，必须拒绝。
  CHECK_FALSE(rawLeaf->addChild(std::move(root)));
  REQUIRE(root != nullptr);
  CHECK(root->parent() == nullptr);
  CHECK(rawLeaf->childCount() == 0);
  CHECK(rawLeaf->parent() == rawMiddle);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 1);
#endif
}

TEST_CASE("Node: childAt out of bounds returns nullptr", "[scene][node][precondition]") {
  ScopedContinueAssertHandler guard;

  auto parent = std::make_unique<yr::scene::Node>();
  CHECK(parent->childAt(0) == nullptr);

  auto child = std::make_unique<yr::scene::Node>();
  yr::scene::Node* const raw = child.get();
  REQUIRE(parent->addChild(std::move(child)));

  CHECK(parent->childAt(0) == raw);
  CHECK(parent->childAt(1) == nullptr);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 2);
#endif
}

TEST_CASE("Node: removeChild rejects null and non-child safely", "[scene][node][precondition]") {
  ScopedContinueAssertHandler guard;

  auto parent = std::make_unique<yr::scene::Node>();
  auto child = std::make_unique<yr::scene::Node>();
  yr::scene::Node* const raw = child.get();

  CHECK(parent->removeChild(nullptr) == nullptr);
  CHECK(parent->removeChild(raw) == nullptr); // 尚未挂到 parent 下

  REQUIRE(parent->addChild(std::move(child)));
  CHECK(parent->removeChild(raw).get() == raw);

  // 已经摘下，再次 remove 应安全返回 nullptr。
  CHECK(parent->removeChild(raw) == nullptr);
  CHECK(parent->childCount() == 0);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount >= 3);
#endif
}