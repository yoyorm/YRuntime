#pragma once

#include <cstdint>
#include <vector>

#include <yr/asset/resource.h>
#include <yr/math/vec3.h>
#include <yr/object/ref.h>

namespace yr::asset {

  enum class AlphaMode : std::uint8_t { kOpaque, kMask, kBlend };

  class Texture final : public Resource {
  public:
    Texture() noexcept : Resource(ResourceKind::kTexture) {}

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8; // width * height * 4
  };

  struct MeshVertex {
    math::Vec3 position{};
    math::Vec3 normal{0.0F, 0.0F, 1.0F};
    float u = 0.0F;
    float v = 0.0F;
  };

  class Mesh final : public Resource {
  public:
    Mesh() noexcept : Resource(ResourceKind::kMesh) {}

    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
  };

  class Material final : public Resource {
  public:
    Material() noexcept : Resource(ResourceKind::kMaterial) {}

    obj::Ref<Texture> baseColorTexture;
    obj::Ref<Texture> normalTexture;
    obj::Ref<Texture> metallicRoughnessTexture;
    obj::Ref<Texture> emissiveTexture;

    math::Vec3 baseColorFactor{1.0F, 1.0F, 1.0F};
    float baseColorAlpha = 1.0F;
    float metallic = 0.0F; // 类默认保持 M9.0a 的非金属默认
    float roughness = 1.0F;
    math::Vec3 emissive{0.0F, 0.0F, 0.0F};
    bool doubleSided = false;
    AlphaMode alphaMode = AlphaMode::kOpaque;
    float alphaCutoff = 0.5F;
  };

} // namespace yr::asset