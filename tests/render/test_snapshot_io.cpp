#include <catch2/catch_test_macros.hpp>

#include <yr/render/snapshot.h>
#include <yr/render/snapshot_io.h>

#include <cstddef>
#include <cstdint>
#include <vector>

using yr::render::AssetHandle;
using yr::render::DrawItem;
using yr::render::LightPod;
using yr::render::LightType;
using yr::render::MaterialAlphaMode;
using yr::render::MaterialUpload;
using yr::render::MeshUpload;
using yr::render::MeshVertexPod;
using yr::render::RenderSnapshot;
using yr::render::ShadowPod;
using yr::render::TextureColorSpace;
using yr::render::TextureUpload;
using yr::render::TransformPod;

namespace {

  bool sameTransform(const TransformPod& lhs, const TransformPod& rhs) {
    for (std::size_t index = 0; index < 16; ++index) {
      if (lhs.m[index] != rhs.m[index]) {
        return false;
      }
    }
    return true;
  }

  bool sameVertex(const MeshVertexPod& lhs, const MeshVertexPod& rhs) {
    for (std::size_t index = 0; index < 3; ++index) {
      if (lhs.position[index] != rhs.position[index] || lhs.normal[index] != rhs.normal[index]) {
        return false;
      }
    }
    return lhs.uv[0] == rhs.uv[0] && lhs.uv[1] == rhs.uv[1];
  }

  bool sameHandles(const std::vector<AssetHandle>& lhs, const std::vector<AssetHandle>& rhs) {
    return lhs == rhs;
  }

  bool sameLight(const LightPod& lhs, const LightPod& rhs) {
    if (lhs.type != rhs.type || lhs.intensity != rhs.intensity || lhs.range != rhs.range) {
      return false;
    }
    for (std::size_t index = 0; index < 3; ++index) {
      if (lhs.position[index] != rhs.position[index] || lhs.direction[index] != rhs.direction[index] ||
          lhs.color[index] != rhs.color[index]) {
        return false;
      }
    }
    return true;
  }

  bool sameShadow(const ShadowPod& lhs, const ShadowPod& rhs) {
    return sameTransform(lhs.viewProj, rhs.viewProj) && lhs.bias == rhs.bias && lhs.texelSize == rhs.texelSize &&
           lhs.enabled == rhs.enabled;
  }

  bool sameSnapshot(const RenderSnapshot& lhs, const RenderSnapshot& rhs) {
    if (lhs.frame_index != rhs.frame_index || !sameTransform(lhs.camera.view, rhs.camera.view) ||
        !sameTransform(lhs.camera.proj, rhs.camera.proj) || lhs.lightCount != rhs.lightCount ||
        !sameShadow(lhs.shadow, rhs.shadow) || lhs.items.size() != rhs.items.size() ||
        lhs.uploads.meshes.size() != rhs.uploads.meshes.size() ||
        lhs.uploads.textures.size() != rhs.uploads.textures.size() ||
        lhs.uploads.materials.size() != rhs.uploads.materials.size() ||
        !sameHandles(lhs.releases.meshes, rhs.releases.meshes) ||
        !sameHandles(lhs.releases.materials, rhs.releases.materials) ||
        !sameHandles(lhs.releases.textures, rhs.releases.textures)) {
      return false;
    }
    for (std::size_t index = 0; index < 3; ++index) {
      if (lhs.camera.eye[index] != rhs.camera.eye[index]) {
        return false;
      }
    }
    for (std::size_t index = 0; index < lhs.lightCount; ++index) {
      if (!sameLight(lhs.lights[index], rhs.lights[index])) {
        return false;
      }
    }
    for (std::size_t index = 0; index < lhs.items.size(); ++index) {
      const DrawItem& a = lhs.items[index];
      const DrawItem& b = rhs.items[index];
      if (a.object != b.object || !sameTransform(a.world, b.world) || a.mesh != b.mesh || a.material != b.material) {
        return false;
      }
    }
    for (std::size_t index = 0; index < lhs.uploads.meshes.size(); ++index) {
      const MeshUpload& a = lhs.uploads.meshes[index];
      const MeshUpload& b = rhs.uploads.meshes[index];
      if (a.mesh != b.mesh || a.indices != b.indices || a.vertices.size() != b.vertices.size()) {
        return false;
      }
      for (std::size_t vertex = 0; vertex < a.vertices.size(); ++vertex) {
        if (!sameVertex(a.vertices[vertex], b.vertices[vertex])) {
          return false;
        }
      }
    }
    for (std::size_t index = 0; index < lhs.uploads.textures.size(); ++index) {
      const TextureUpload& a = lhs.uploads.textures[index];
      const TextureUpload& b = rhs.uploads.textures[index];
      if (a.texture != b.texture || a.width != b.width || a.height != b.height || a.colorSpace != b.colorSpace ||
          a.rgba8 != b.rgba8) {
        return false;
      }
    }
    for (std::size_t index = 0; index < lhs.uploads.materials.size(); ++index) {
      const MaterialUpload& a = lhs.uploads.materials[index];
      const MaterialUpload& b = rhs.uploads.materials[index];
      if (a.material != b.material || a.baseColorTexture != b.baseColorTexture || a.normalTexture != b.normalTexture ||
          a.metallicRoughnessTexture != b.metallicRoughnessTexture || a.emissiveTexture != b.emissiveTexture ||
          a.metallic != b.metallic || a.roughness != b.roughness || a.normalScale != b.normalScale ||
          a.alphaCutoff != b.alphaCutoff || a.alphaMode != b.alphaMode) {
        return false;
      }
      for (std::size_t factor = 0; factor < 4; ++factor) {
        if (a.baseColorFactor[factor] != b.baseColorFactor[factor]) {
          return false;
        }
      }
      for (std::size_t component = 0; component < 3; ++component) {
        if (a.emissiveFactor[component] != b.emissiveFactor[component]) {
          return false;
        }
      }
    }
    return true;
  }

  RenderSnapshot makeSnapshot() {
    RenderSnapshot snapshot;
    snapshot.frame_index = 12345;
    for (std::size_t index = 0; index < 16; ++index) {
      snapshot.camera.view.m[index] = static_cast<float>(index);
      snapshot.camera.proj.m[index] = static_cast<float>(index) * 0.5F;
    }
    snapshot.camera.eye[0] = 2.0F;
    snapshot.camera.eye[1] = -3.0F;
    snapshot.camera.eye[2] = 4.0F;

    snapshot.lightCount = 2;
    snapshot.lights[0].type = LightType::kDirectional;
    snapshot.lights[0].position[0] = 1.0F;
    snapshot.lights[0].position[1] = 2.0F;
    snapshot.lights[0].position[2] = 3.0F;
    snapshot.lights[0].direction[0] = -0.5F;
    snapshot.lights[0].direction[1] = -0.5F;
    snapshot.lights[0].direction[2] = -0.5F;
    snapshot.lights[0].color[0] = 0.9F;
    snapshot.lights[0].color[1] = 0.8F;
    snapshot.lights[0].color[2] = 0.7F;
    snapshot.lights[0].intensity = 2.5F;
    snapshot.lights[0].range = 12.0F;
    snapshot.lights[1].type = LightType::kPoint;
    snapshot.lights[1].position[0] = -1.0F;
    snapshot.lights[1].position[1] = -2.0F;
    snapshot.lights[1].position[2] = -3.0F;
    snapshot.lights[1].direction[0] = 0.25F;
    snapshot.lights[1].direction[1] = 0.5F;
    snapshot.lights[1].direction[2] = 0.75F;
    snapshot.lights[1].color[0] = 0.4F;
    snapshot.lights[1].color[1] = 0.5F;
    snapshot.lights[1].color[2] = 0.6F;
    snapshot.lights[1].intensity = 3.5F;
    snapshot.lights[1].range = 7.0F;

    snapshot.shadow.enabled = 1;
    snapshot.shadow.bias = 0.0025F;
    snapshot.shadow.texelSize = 1.0F / 1024.0F;
    for (std::size_t index = 0; index < 16; ++index) {
      snapshot.shadow.viewProj.m[index] = static_cast<float>(index) * 0.25F;
    }

    for (std::uint64_t index = 0; index < 3; ++index) {
      DrawItem item;
      item.object.value = 100 + index;
      item.mesh.value = 200 + index;
      item.material.value = 300 + index;
      for (std::size_t element = 0; element < 16; ++element) {
        item.world.m[element] = static_cast<float>(index * 16 + element);
      }
      snapshot.items.push_back(item);
    }

    MeshUpload mesh;
    mesh.mesh.value = 41;
    MeshVertexPod vertex;
    vertex.position[0] = 1.0F;
    vertex.position[1] = 2.0F;
    vertex.position[2] = 3.0F;
    vertex.normal[0] = 0.0F;
    vertex.normal[1] = 1.0F;
    vertex.normal[2] = 0.0F;
    vertex.uv[0] = 0.25F;
    vertex.uv[1] = 0.75F;
    mesh.vertices.push_back(vertex);
    mesh.indices = {0, 0, 0};
    snapshot.uploads.meshes.push_back(mesh);

    TextureUpload texture;
    texture.texture.value = 42;
    texture.width = 1;
    texture.height = 1;
    texture.colorSpace = TextureColorSpace::kLinear;
    texture.rgba8 = {1, 2, 3, 4};
    snapshot.uploads.textures.push_back(texture);

    MaterialUpload material;
    material.material.value = 43;
    material.baseColorTexture.value = 42;
    material.normalTexture.value = 44;
    material.metallicRoughnessTexture.value = 45;
    material.emissiveTexture.value = 46;
    material.baseColorFactor[0] = 0.1F;
    material.baseColorFactor[1] = 0.2F;
    material.baseColorFactor[2] = 0.3F;
    material.baseColorFactor[3] = 0.4F;
    material.metallic = 0.5F;
    material.roughness = 0.6F;
    material.normalScale = 0.75F;
    material.emissiveFactor[0] = 0.05F;
    material.emissiveFactor[1] = 0.1F;
    material.emissiveFactor[2] = 0.15F;
    material.alphaCutoff = 0.35F;
    material.alphaMode = MaterialAlphaMode::kMask;
    snapshot.uploads.materials.push_back(material);

    snapshot.releases.meshes = {{51}, {52}};
    snapshot.releases.materials = {{53}};
    snapshot.releases.textures = {{54}, {55}};
    return snapshot;
  }

} // namespace

TEST_CASE("snapshot io: round-trip preserves every field", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();

  const std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  CHECK(yr::render::snapshotByteSize(original) == encoded.size());
  RenderSnapshot decoded;
  REQUIRE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), decoded));

  CHECK(sameSnapshot(original, decoded));
}

TEST_CASE("snapshot io: encoding is byte-for-byte deterministic", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();

  const std::vector<std::uint8_t> first = yr::render::encodeSnapshot(original);
  const std::vector<std::uint8_t> second = yr::render::encodeSnapshot(original);

  CHECK(first == second);
}

TEST_CASE("snapshot io: tampered magic is rejected and out untouched", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();
  std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  encoded[0] = static_cast<std::uint8_t>(encoded[0] ^ 0xFFU);
  const RenderSnapshot sentinel = makeSnapshot();

  RenderSnapshot out = sentinel;
  CHECK_FALSE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), out));
  CHECK(sameSnapshot(out, sentinel));
}

TEST_CASE("snapshot io: tampered version is rejected and out untouched", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();
  std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  encoded[6] = static_cast<std::uint8_t>(encoded[6] ^ 0xFFU);
  const RenderSnapshot sentinel = makeSnapshot();

  RenderSnapshot out = sentinel;
  CHECK_FALSE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), out));
  CHECK(sameSnapshot(out, sentinel));
}

TEST_CASE("snapshot io: truncated buffer is rejected and out untouched", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();
  const std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  const std::vector<std::uint8_t> truncated(encoded.begin(), encoded.end() - 1);
  const RenderSnapshot sentinel = makeSnapshot();

  RenderSnapshot out = sentinel;
  CHECK_FALSE(yr::render::decodeSnapshot(truncated.data(), truncated.size(), out));
  CHECK(sameSnapshot(out, sentinel));
}

TEST_CASE("snapshot io: trailing byte is rejected and out untouched", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();
  std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  encoded.push_back(0);
  const RenderSnapshot sentinel = makeSnapshot();

  RenderSnapshot out = sentinel;
  CHECK_FALSE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), out));
  CHECK(sameSnapshot(out, sentinel));
}

TEST_CASE("snapshot io: inconsistent texture dimensions are rejected", "[render][snapshot_io]") {
  RenderSnapshot malformed = makeSnapshot();
  malformed.uploads.textures[0].width = 2;
  const std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(malformed);
  RenderSnapshot out = makeSnapshot();

  CHECK_FALSE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), out));
  CHECK(sameSnapshot(out, makeSnapshot()));
}

TEST_CASE("snapshot io: empty snapshot round-trips", "[render][snapshot_io]") {
  const RenderSnapshot original{};

  const std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  RenderSnapshot decoded;
  REQUIRE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), decoded));

  CHECK(yr::render::snapshotByteSize(original) == encoded.size());
  CHECK(decoded.frame_index == 0);
  CHECK(decoded.lightCount == 0);
  CHECK(decoded.shadow.enabled == 0);
  CHECK(decoded.items.empty());
}

TEST_CASE("snapshot io: light count above the limit is rejected and out untouched", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();
  std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  constexpr std::size_t kLightCountOffset = 6 + 4 + 8 + (2 * 16 * 4) + (3 * 4);
  encoded[kLightCountOffset] = 9;
  const RenderSnapshot sentinel = makeSnapshot();

  RenderSnapshot out = sentinel;
  CHECK_FALSE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), out));
  CHECK(sameSnapshot(out, sentinel));
}

TEST_CASE("snapshot io: invalid light type is rejected and out untouched", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();
  std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  constexpr std::size_t kFirstLightTypeOffset = 6 + 4 + 8 + (2 * 16 * 4) + (3 * 4) + 8;
  encoded[kFirstLightTypeOffset] = 2;
  const RenderSnapshot sentinel = makeSnapshot();

  RenderSnapshot out = sentinel;
  CHECK_FALSE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), out));
  CHECK(sameSnapshot(out, sentinel));
}
