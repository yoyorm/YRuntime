#pragma once

#include <array>
#include <cstddef>
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

  // 相机 view/proj 矩阵与世界空间眼点。
  struct CameraPod {
    TransformPod view{};
    TransformPod proj{};
    float eye[3]{0.0F, 0.0F, 0.0F};
  };

  struct MeshVertexPod {
    float position[3]{};
    float normal[3]{0.0F, 0.0F, 1.0F};
    float uv[2]{};
  };

  enum class TextureColorSpace : std::uint8_t { kSrgb, kLinear };
  enum class MaterialAlphaMode : std::uint8_t { kOpaque = 0, kMask = 1 };
  enum class LightType : std::uint32_t { kDirectional = 0, kPoint = 1 };

  struct LightPod {
    LightType type = LightType::kDirectional;
    float position[3]{};
    float direction[3]{0.0F, 0.0F, -1.0F};
    float color[3]{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    float range = 10.0F;
  };

  struct ShadowPod {
    TransformPod viewProj{};
    float bias = 0.001F;
    float texelSize = 1.0F / 2048.0F;
    std::uint32_t enabled = 0;
  };

  struct MeshUpload {
    AssetHandle mesh{};
    std::vector<MeshVertexPod> vertices;
    std::vector<std::uint32_t> indices;
  };

  struct TextureUpload {
    AssetHandle texture{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureColorSpace colorSpace = TextureColorSpace::kSrgb;
    std::vector<std::uint8_t> rgba8;
  };

  struct MaterialUpload {
    AssetHandle material{};
    AssetHandle baseColorTexture{};
    AssetHandle normalTexture{};
    AssetHandle metallicRoughnessTexture{};
    AssetHandle emissiveTexture{};
    float baseColorFactor[4]{1.0F, 1.0F, 1.0F, 1.0F};
    float metallic = 0.0F;
    float roughness = 1.0F;
    float normalScale = 1.0F;
    float emissiveFactor[3]{0.0F, 0.0F, 0.0F};
    float alphaCutoff = 0.5F;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::kOpaque;
  };

  struct RenderUploads {
    std::vector<MeshUpload> meshes;
    std::vector<TextureUpload> textures;
    std::vector<MaterialUpload> materials;
  };

  struct RenderReleases {
    std::vector<AssetHandle> meshes;
    std::vector<AssetHandle> materials;
    std::vector<AssetHandle> textures;
  };

  struct RenderSnapshot {
    static constexpr std::size_t kMaxLights = 8;

    std::uint64_t frame_index = 0;
    CameraPod camera{};
    std::array<LightPod, kMaxLights> lights{};
    std::uint32_t lightCount = 0;
    ShadowPod shadow{};
    std::vector<DrawItem> items;
    RenderUploads uploads;
    RenderReleases releases;
  };

} // namespace yr::render

static_assert(std::is_trivially_copyable_v<yr::render::AssetHandle>);
static_assert(std::is_trivially_copyable_v<yr::render::RenderObjectId>);
static_assert(std::is_trivially_copyable_v<yr::render::TransformPod>);
static_assert(std::is_trivially_copyable_v<yr::render::DrawItem>);
static_assert(std::is_trivially_copyable_v<yr::render::CameraPod>);
static_assert(std::is_trivially_copyable_v<yr::render::MeshVertexPod>);
static_assert(sizeof(yr::render::MeshVertexPod) == 32);
static_assert(std::is_trivially_copyable_v<yr::render::LightPod>);
static_assert(std::is_trivially_copyable_v<yr::render::ShadowPod>);
static_assert(std::is_trivially_copyable_v<yr::render::MaterialUpload>);
// RenderSnapshot 含 std::vector，刻意不对其本体断言 POD。
