#include <catch2/catch_test_macros.hpp>

#include <yr/core/assert.h>
#include <yr/core/object_id.h>
#include <yr/core/string_id.h>
#include <yr/core/string_interner.h>
#include <yr/object/object.h>
#include <yr/object/variant.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace {

  using yr::core::ObjectID;
  using yr::core::StringId;
  using yr::core::StringInterner;
  using yr::obj::Variant;

  class TestObject final : public yr::obj::Object {};

} // namespace

// ============================================================================
// 编译期契约：加入 std::string（堆）后不再平凡，但必须可拷贝/移动/赋值
// ============================================================================
static_assert(!std::is_trivially_copyable_v<Variant>); // 持有 unique_ptr，不可能平凡拷贝
static_assert(!std::is_trivially_destructible_v<Variant>);
static_assert(std::is_copy_constructible_v<Variant>);
static_assert(std::is_move_constructible_v<Variant>);
static_assert(std::is_copy_assignable_v<Variant>);
static_assert(std::is_move_assignable_v<Variant>);

// 只接受精确的 5 种类型；int/float 不走隐式提升（避免 Variant(0) 歧义）
static_assert(std::is_constructible_v<Variant, bool>);
static_assert(std::is_constructible_v<Variant, std::int64_t>);
static_assert(std::is_constructible_v<Variant, double>);
static_assert(std::is_constructible_v<Variant, StringId>);
static_assert(std::is_constructible_v<Variant, ObjectID>);
static_assert(!std::is_constructible_v<Variant, int>);
static_assert(std::is_constructible_v<Variant, std::string>);
static_assert(std::is_convertible_v<std::int64_t, Variant>); // 构造非 explicit

// 含堆成员后 Variant 不再是字面类型，无法再定义 constexpr Variant 对象；
// 各类型的构造/查询改由下面的运行期用例覆盖。

// ============================================================================
// 默认值
// ============================================================================

TEST_CASE("variant: 默认构造为 null", "[object][variant]") {
  const Variant value;

  CHECK(value.type() == Variant::Type::kNull);
  CHECK(value.isNil());
  CHECK_FALSE(value.is<bool>());
  CHECK_FALSE(value.is<std::int64_t>());
}

// ============================================================================
// 每种类型构造 + round-trip
// ============================================================================

TEST_CASE("variant: bool 构造与往返", "[object][variant]") {
  const Variant value(true);

  CHECK(value.type() == Variant::Type::kBool);
  CHECK(value.is<bool>());
  CHECK(value.as<bool>());
  CHECK(value.tryAs<bool>().value());
}

TEST_CASE("variant: int64 构造与往返", "[object][variant]") {
  const Variant value(std::int64_t{-7});

  CHECK(value.type() == Variant::Type::kInt);
  CHECK(value.is<std::int64_t>());
  CHECK(value.as<std::int64_t>() == -7);
  CHECK(value.tryAs<std::int64_t>().value() == -7);
}

TEST_CASE("variant: double 构造与往返", "[object][variant]") {
  const Variant value(3.25);

  CHECK(value.type() == Variant::Type::kFloat);
  CHECK(value.is<double>());
  CHECK(value.as<double>() == 3.25);
  CHECK(value.tryAs<double>().value() == 3.25);
}

TEST_CASE("variant: StringId 构造与往返", "[object][variant]") {
  StringInterner& interner = yr::core::globalStringInterner();
  const StringId player = interner.intern("variant_test_player");

  const Variant value(player);
  REQUIRE(player.valid());
  CHECK(value.type() == Variant::Type::kStringId);
  CHECK(value.is<StringId>());
  CHECK(value.as<StringId>() == player);
  CHECK(value.tryAs<StringId>().value() == player);
}

TEST_CASE("variant: ObjectID 构造与往返", "[object][variant]") {
  const TestObject object;
  const ObjectID id = object.id();

  const Variant value(id);
  REQUIRE(id.valid());
  CHECK(value.type() == Variant::Type::kObjectID);
  CHECK(value.is<ObjectID>());
  CHECK(value.as<ObjectID>() == id);
  CHECK(value.tryAs<ObjectID>().value() == id);
}

// ============================================================================
// 拷贝 / 移动
// ============================================================================

TEST_CASE("variant: 拷贝保持类型与值，且互相独立", "[object][variant]") {
  const Variant original(std::int64_t{11});
  Variant copy = original;

  CHECK(copy.type() == original.type());
  CHECK(copy == original);

  copy.set(std::int64_t{22});
  CHECK(copy.as<std::int64_t>() == 22);
  CHECK(original.as<std::int64_t>() == 11); // 原对象不受影响
}

TEST_CASE("variant: 移动保持类型与值", "[object][variant]") {
  Variant source(3.5);
  Variant moved = std::move(source);

  CHECK(moved.type() == Variant::Type::kFloat);
  CHECK(moved.as<double>() == 3.5);
  // Variant-1 是平凡类型，移动等价于拷贝，不保证源被清空（Variant-2 引入字符串后才有此语义）
}

// ============================================================================
// 类型查询 / 类型混淆检测
// ============================================================================

TEST_CASE("variant: is<T> 对不同类型返回 false", "[object][variant]") {
  const Variant value(std::int64_t{1});

  CHECK(value.is<std::int64_t>());
  CHECK_FALSE(value.is<bool>());
  CHECK_FALSE(value.is<double>());
  CHECK_FALSE(value.is<StringId>());
  CHECK_FALSE(value.is<ObjectID>());
}

#if YR_ENABLE_ASSERTS
namespace {

  int g_assertCount = 0;

  yr::core::AssertAction countingHandler(const yr::core::AssertInfo&) noexcept {
    ++g_assertCount;
    return yr::core::AssertAction::kContinue;
  }

} // namespace
#endif

TEST_CASE("variant: tryAs 类型不符返回 nullopt，不触发断言", "[object][variant]") {
#if YR_ENABLE_ASSERTS
  g_assertCount = 0;
  yr::core::setAssertHandler(countingHandler);
#endif
  const Variant value(std::int64_t{5});

  CHECK_FALSE(value.tryAs<double>().has_value());
  CHECK_FALSE(value.tryAs<StringId>().has_value());
  CHECK(value.tryAs<std::int64_t>().value() == 5);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount == 0);
  yr::core::setAssertHandler(nullptr);
#endif
}

TEST_CASE("variant: as 类型不符返回默认值并触发断言", "[object][variant][assert]") {
#if YR_ENABLE_ASSERTS
  g_assertCount = 0;
  yr::core::setAssertHandler(countingHandler);
#endif
  const Variant value(std::int64_t{5});

  CHECK(value.as<bool>() == false);
  CHECK(value.as<double>() == 0.0);

#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount == 2);
  yr::core::setAssertHandler(nullptr);
#endif
}

// ============================================================================
// setter
// ============================================================================

TEST_CASE("variant: set 就地改写类型与值", "[object][variant]") {
  Variant value;
  REQUIRE(value.isNil());

  value.set(std::int64_t{9});
  CHECK(value.type() == Variant::Type::kInt);
  CHECK(value.as<std::int64_t>() == 9);

  value.set(true);
  CHECK(value.type() == Variant::Type::kBool);
  CHECK(value.as<bool>());

  value.set(1.25);
  CHECK(value.type() == Variant::Type::kFloat);
  CHECK(value.as<double>() == 1.25);
}

TEST_CASE("variant: reset 回到 null", "[object][variant]") {
  Variant value(std::int64_t{1});
  value.reset();

  CHECK(value.type() == Variant::Type::kNull);
  CHECK(value.isNil());
}

// ============================================================================
// 相等比较
// ============================================================================

TEST_CASE("variant: 两个 null 相等", "[object][variant]") {
  const Variant first;
  const Variant second;

  CHECK(first == second);
  CHECK_FALSE(first != second);
}

TEST_CASE("variant: 同类型同值相等，同类型异值不等", "[object][variant]") {
  CHECK(Variant(std::int64_t{3}) == Variant(std::int64_t{3}));
  CHECK(Variant(std::int64_t{3}) != Variant(std::int64_t{4}));

  CHECK(Variant(true) == Variant(true));
  CHECK(Variant(true) != Variant(false));

  CHECK(Variant(1.5) == Variant(1.5));
  CHECK(Variant(1.5) != Variant(2.5));
}

TEST_CASE("variant: 不同类型即使数值相同也不等", "[object][variant]") {
  CHECK_FALSE(Variant(true) == Variant(std::int64_t{1}));
  CHECK_FALSE(Variant(std::int64_t{1}) == Variant(1.0));
  CHECK(Variant(std::int64_t{1}) != Variant(1.0));
  CHECK_FALSE(Variant{} == Variant(std::int64_t{0}));
}

TEST_CASE("variant: StringId 相等按身份判定", "[object][variant]") {
  StringInterner& interner = yr::core::globalStringInterner();
  const StringId player = interner.intern("variant_eq_player");
  const StringId enemy = interner.intern("variant_eq_enemy");

  CHECK(Variant(player) == Variant(player));
  CHECK(Variant(player) == Variant(interner.intern("variant_eq_player")));
  CHECK(Variant(player) != Variant(enemy));
  // StringId 是身份类型：无效 StringId 也参与比较
  CHECK(Variant(StringId{}) == Variant(StringId{}));
}

TEST_CASE("variant: ObjectID 相等按身份判定", "[object][variant]") {
  const TestObject first;
  const TestObject second;

  CHECK(Variant(first.id()) == Variant(first.id()));
  CHECK_FALSE(Variant(first.id()) == Variant(second.id()));
  CHECK(Variant(first.id()) != Variant(second.id()));
}

// ============================================================================
// std::string（堆类型）：构造 / 深拷贝 / 移动 / 比较
// ============================================================================

TEST_CASE("variant: std::string 构造与往返", "[object][variant][string]") {
  const Variant value(std::string("hello"));

  CHECK(value.type() == Variant::Type::kString);
  CHECK(value.is<std::string>());
  CHECK(value.as<std::string>() == "hello");
  CHECK(value.tryAs<std::string>().value() == "hello");
}

TEST_CASE("variant: string 拷贝是深拷贝，互相独立", "[object][variant][string]") {
  const Variant original(std::string("hello"));
  Variant copy = original;

  CHECK(copy == original);

  copy.set(std::string("world"));
  CHECK(copy.as<std::string>() == "world");
  CHECK(original.as<std::string>() == "hello"); // 原对象不受影响
}

TEST_CASE("variant: string 移动后源变为 null", "[object][variant][string]") {
  Variant source(std::string("hello"));
  Variant moved = std::move(source);

  CHECK(moved.type() == Variant::Type::kString);
  CHECK(moved.as<std::string>() == "hello");
  CHECK(source.isNil());
}

TEST_CASE("variant: string 拷贝/移动/自赋值覆盖旧类型", "[object][variant][string]") {
  Variant source(std::string("a"));
  Variant target(std::int64_t{1});

  target = source; // 拷贝赋值，覆盖旧的 kInt
  CHECK(target.type() == Variant::Type::kString);
  CHECK(target.as<std::string>() == "a");
  CHECK(source.as<std::string>() == "a"); // 源不变

  Variant moved;
  moved = std::move(source); // 移动赋值
  CHECK(moved.as<std::string>() == "a");
  CHECK(source.isNil());

  Variant& alias = moved; // 经别名自赋值，规避 clang -Wself-assign-overloaded
  moved = alias;
  CHECK(moved.as<std::string>() == "a");
}

TEST_CASE("variant: string 相等按内容解引用比较", "[object][variant][string]") {
  CHECK(Variant(std::string("same")) == Variant(std::string("same")));
  CHECK(Variant(std::string("same")) != Variant(std::string("diff")));
  CHECK_FALSE(Variant(std::string("1")) == Variant(std::int64_t{1})); // 跨类型
}

TEST_CASE("variant: reset 释放 string 后回到 null", "[object][variant][string]") {
  Variant value(std::string("hello"));
  value.reset();

  CHECK(value.isNil());
  CHECK(value.type() == Variant::Type::kNull);
}