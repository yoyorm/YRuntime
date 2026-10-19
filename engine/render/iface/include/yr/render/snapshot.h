#pragma once

#include <cstdint>
#include <type_traits>
#include <vector>

#include <yr/render/asset_handle.h>

namespace yr::render {

  // 渲染对象身份：承载 ObjectID 的值
  struct RenderObjectId {
    std::uint64_t value = 0;
    [[nodiscard]] constexpr bool operator==(const RenderObjectId&) const noexcept = default;
  };

  // 布局同 yr::math::Mat4：column-major、列向量 v' = M * v、右手 Y-up、深度 [0,1]。
  // 默认 identity（同 Mat4），不留全零退化矩阵。
  struct TransformPod {
    float m[16]{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
  };

  struct DrawItem {
    RenderObjectId object{};
    TransformPod world{};
    AssetHandle mesh{};     // M5：内建/无效，未就绪语义推迟到 M9
    AssetHandle material{}; // 同上
  };

  // 相机：view/proj 两个矩阵；M5 单相机、固定默认值。
  struct CameraPod {
    TransformPod view{};
    TransformPod proj{};
  };

  struct RenderSnapshot {
    std::uint64_t frame_index = 0;
    CameraPod camera{};
    std::vector<DrawItem> items;
  };

} // namespace yr::render

static_assert(std::is_trivially_copyable_v<yr::render::AssetHandle>);
static_assert(std::is_trivially_copyable_v<yr::render::RenderObjectId>);
static_assert(std::is_trivially_copyable_v<yr::render::TransformPod>);
static_assert(std::is_trivially_copyable_v<yr::render::DrawItem>);
static_assert(std::is_trivially_copyable_v<yr::render::CameraPod>);
// RenderSnapshot 含 std::vector，刻意不对其本体断言 POD。