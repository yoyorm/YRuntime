#include <catch2/catch_test_macros.hpp>

#include <yr/render/asset_handle.h>
#include <yr/render/snapshot.h>

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

using yr::render::CameraPod;
using yr::render::DrawItem;
using yr::render::RenderObjectId;
using yr::render::RenderSnapshot;
using yr::render::TransformPod;

// 元素级 POD 断言在 snapshot.h 里；这里锁定测试侧的等价契约与尺寸。
static_assert(std::is_trivially_copyable_v<RenderObjectId>);
static_assert(std::is_trivially_copyable_v<TransformPod>);
static_assert(std::is_trivially_copyable_v<DrawItem>);
static_assert(std::is_trivially_copyable_v<CameraPod>);
static_assert(sizeof(TransformPod) == 64);

namespace {

  bool transformIsIdentity(const TransformPod& transform) {
    for (std::size_t index = 0; index < 16; ++index) {
      const float expected = (index % 5 == 0) ? 1.0F : 0.0F;
      if (transform.m[index] != expected) {
        return false;
      }
    }
    return true;
  }

} // namespace

TEST_CASE("snapshot: default snapshot is empty and identity camera", "[render][snapshot]") {
  const RenderSnapshot snapshot{};

  CHECK(snapshot.frame_index == 0);
  CHECK(snapshot.items.empty());
  CHECK(transformIsIdentity(snapshot.camera.view));
  CHECK(transformIsIdentity(snapshot.camera.proj));
  CHECK(snapshot.camera.eye[0] == 0.0F);
  CHECK(snapshot.camera.eye[1] == 0.0F);
  CHECK(snapshot.camera.eye[2] == 0.0F);
}

TEST_CASE("snapshot: default draw item is identity with invalid handles", "[render][snapshot]") {
  const DrawItem item{};

  CHECK(item.object.value == 0);
  CHECK(item.mesh.value == 0);
  CHECK(item.material.value == 0);
  CHECK(transformIsIdentity(item.world));
}

TEST_CASE("snapshot: default transform pod is identity", "[render][snapshot]") {
  const TransformPod transform{};

  CHECK(transformIsIdentity(transform));
}
