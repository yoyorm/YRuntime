#include <catch2/catch_test_macros.hpp>

#include <yr/render/asset_handle.h>

#include <cstdint>
#include <type_traits>

using yr::render::AssetHandle;

static_assert(std::is_trivially_copyable_v<AssetHandle>);
static_assert(std::is_default_constructible_v<AssetHandle>);
static_assert(sizeof(AssetHandle) == sizeof(std::uint64_t));
// 强类型：裸整数不得隐式转换（否则类型安全与"单一来源"都失效）。
// 注：聚合类型在 C++20 允许带括号的逐字段初始化，故 is_constructible 为 true，只锁隐式转换。
static_assert(!std::is_convertible_v<std::uint64_t, AssetHandle>);

TEST_CASE("asset handle: default is invalid", "[render][asset_handle]") {
  const AssetHandle handle{};

  CHECK_FALSE(handle.isValid());
  CHECK(handle.value == 0);
}

TEST_CASE("asset handle: non-zero value is valid", "[render][asset_handle]") {
  const AssetHandle handle{.value = 42};

  CHECK(handle.isValid());
  CHECK(handle.value == 42);
}

TEST_CASE("asset handle: equality compares value", "[render][asset_handle]") {
  CHECK(AssetHandle{.value = 7} == AssetHandle{.value = 7});
  CHECK(AssetHandle{.value = 7} != AssetHandle{.value = 8});
  CHECK(AssetHandle{} == AssetHandle{});
}