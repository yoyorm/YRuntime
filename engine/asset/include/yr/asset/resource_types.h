#pragma once

#include <cstdint>
#include <vector>

#include <yr/asset/resource.h>
#include <yr/math/vec3.h>
#include <yr/object/ref.h>

namespace yr::asset {

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

    obj::Ref<Texture> baseColor;
    math::Vec3 baseColorFactor{1.0F, 1.0F, 1.0F};
    float metallic = 0.0F;
    float roughness = 1.0F;
    math::Vec3 emissive{0.0F, 0.0F, 0.0F};
  };

} // namespace yr::asset