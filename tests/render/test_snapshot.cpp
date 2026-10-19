#include <catch2/catch_test_macros.hpp>

#include <yr/render/snapshot.h>

#include <cstdint>
#include <type_traits>

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