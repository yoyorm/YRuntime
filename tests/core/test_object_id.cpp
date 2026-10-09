#include <catch2/catch_test_macros.hpp>

#include <yr/core/handle.h>
#include <yr/core/object_id.h>
#include <yr/core/string_id.h>

#include <cstdint>
#include <type_traits>

namespace {

  using IntHandle = yr::core::Handle<int>;
  using ObjectID = yr::core::ObjectID;
  using StringId = yr::core::StringId;

} // namespace

static_assert(sizeof(ObjectID) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<ObjectID>);
static_assert(std::is_default_constructible_v<ObjectID>);
static_assert(!std::is_constructible_v<ObjectID, std::uint64_t>);
static_assert(!std::is_convertible_v<std::uint64_t, ObjectID>);
static_assert(!std::is_convertible_v<ObjectID, IntHandle>);
static_assert(!std::is_convertible_v<IntHandle, ObjectID>);
static_assert(!std::is_convertible_v<ObjectID, StringId>);
static_assert(!std::is_convertible_v<StringId, ObjectID>);
static_assert(yr::core::ObjectIDBits::kIndexBits + yr::core::ObjectIDBits::kGenerationBits == 64);
static_assert(yr::core::ObjectIDBits::kGenerationMask == ((std::uint64_t{1} << 40) - 1));

// encode() 就是 ObjectID::make() 的位打包实现：generation 先掩码到 40 位，
// 因此超宽 generation 不会污染 index，也不会让解码值与 ObjectDB 存储值分叉。
static_assert(yr::core::ObjectIDBits::encode(7, 5) == ((std::uint64_t{5} << 24) | 7));
static_assert(yr::core::ObjectIDBits::encode(0, 1) == (std::uint64_t{1} << 24)); // index=0 + generation=1 != 0，合法
static_assert(yr::core::ObjectIDBits::encode(0, std::uint64_t{1} << 40) == 0);   // 高位被截断
static_assert(yr::core::ObjectIDBits::encode(3, (std::uint64_t{1} << 40) + 5) == ((std::uint64_t{5} << 24) | 3));

// generation 自增契约：规范化到 40 位，回绕后跳过 0（0 保留给 invalid）。
static_assert(yr::core::ObjectIDBits::nextGeneration(1) == 2);
static_assert(yr::core::ObjectIDBits::nextGeneration(41) == 42);
static_assert(yr::core::ObjectIDBits::nextGeneration((std::uint64_t{1} << 40) - 1) == 1);
static_assert(yr::core::ObjectIDBits::nextGeneration(0) == 1);

TEST_CASE("object id: default value is invalid", "[core][object_id]") {
  const ObjectID id{};

  CHECK_FALSE(id.valid());
  CHECK(id == ObjectID::invalid());
  CHECK(id.raw() == 0);
  CHECK(id.index() == 0);
  CHECK(id.generation() == 0);
}

TEST_CASE("object id: encode 的 index/generation 可无损解码（掩码一致）", "[core][object_id]") {
  using Bits = yr::core::ObjectIDBits;
  constexpr std::uint64_t kMaxGeneration = (std::uint64_t{1} << 40) - 1;

  const std::uint64_t raw = Bits::encode(0x00ABCDEF, kMaxGeneration);
  CHECK((raw & Bits::kIndexMask) == 0x00ABCDEF);
  CHECK((raw >> Bits::kIndexBits) == kMaxGeneration);

  // 超宽 generation 被截断到低 40 位，解码回同一规范化值（而不是未定义的高位）。
  const std::uint64_t wrapped = Bits::encode(5, kMaxGeneration + 3);
  CHECK((wrapped & Bits::kIndexMask) == 5);
  CHECK((wrapped >> Bits::kIndexBits) == 2);
}

TEST_CASE("object id: generation 回绕从 0 跳到 1（无需循环 2^40 次）", "[core][object_id]") {
  using Bits = yr::core::ObjectIDBits;
  constexpr std::uint64_t kMaxGeneration = (std::uint64_t{1} << 40) - 1;

  CHECK(Bits::nextGeneration(1) == 2);
  CHECK(Bits::nextGeneration(41) == 42);
  CHECK(Bits::nextGeneration(kMaxGeneration) == 1); // 最大 + 1：回绕并跳过 0
  CHECK(Bits::nextGeneration(0) == 1);              // 0 保留给 invalid，不作为合法 generation
}
