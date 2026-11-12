#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <yr/asset/asset_uid.h>
#include <yr/asset/gltf_loader.h>
#include <yr/asset/resource_types.h>
#include <yr/math/vec3.h>
#include <yr/object/ref.h>

namespace {

  using yr::asset::AlphaMode;
  using yr::asset::AssetUID;
  using yr::asset::GltfLoadResult;
  using yr::asset::GltfNode;
  using yr::asset::GltfResourceHooks;
  using yr::asset::LoadState;
  using yr::asset::Material;
  using yr::asset::Mesh;
  using yr::asset::Resource;
  using yr::asset::Texture;

  [[nodiscard]] std::size_t countPrimitives(const GltfNode& node) {
    std::size_t count = node.primitives.size();
    for (const GltfNode& child : node.children) {
      count += countPrimitives(child);
    }
    return count;
  }

  void checkNodeResources(const GltfNode& node) {
    for (const auto& primitive : node.primitives) {
      CHECK(primitive.mesh.valid());
      CHECK(primitive.material.valid());
      CHECK(primitive.mesh->state() == LoadState::kReady);
      CHECK(primitive.mesh->uid().valid());
      CHECK(primitive.material->state() == LoadState::kReady);
      CHECK(primitive.material->uid().valid());

      const auto checkTexture = [](const yr::obj::Ref<Texture>& texture) {
        if (texture.valid()) {
          CHECK(texture->state() == LoadState::kReady);
          CHECK(texture->uid().valid());
        }
      };
      checkTexture(primitive.material->baseColorTexture);
      checkTexture(primitive.material->normalTexture);
      checkTexture(primitive.material->metallicRoughnessTexture);
      checkTexture(primitive.material->emissiveTexture);
    }
    for (const GltfNode& child : node.children) {
      checkNodeResources(child);
    }
  }

} // namespace

TEST_CASE("gltf loader: data-URI triangle uses defaults and sequential indices", "[asset][gltf]") {
  const GltfLoadResult result = yr::asset::loadGltfScene("res://triangle.gltf", YR_TEST_FIXTURE_ROOT);
  CAPTURE(result.error);
  REQUIRE(result.ok);
  CHECK(result.warnings.empty());

  REQUIRE(result.scene.roots.size() == 1);
  const GltfNode& root = result.scene.roots[0];
  CHECK(root.name == "Triangle");
  REQUIRE(root.primitives.size() == 1);

  const auto& primitive = root.primitives[0];
  REQUIRE(primitive.mesh.valid());
  REQUIRE(primitive.material.valid());
  CHECK(primitive.mesh->kind() == yr::asset::ResourceKind::kMesh);
  CHECK(primitive.material->kind() == yr::asset::ResourceKind::kMaterial);
  CHECK(primitive.mesh->state() == LoadState::kReady);
  CHECK(primitive.material->state() == LoadState::kReady);
  CHECK(primitive.mesh->uid().valid());
  CHECK(primitive.material->uid().valid());

  REQUIRE(primitive.mesh->vertices.size() == 3);
  const std::vector<std::uint32_t> expectedIndices{0, 1, 2};
  CHECK(primitive.mesh->indices == expectedIndices);
  CHECK(primitive.mesh->vertices[0].position == (yr::math::Vec3{0.0F, 0.0F, 0.0F}));
  CHECK(primitive.mesh->vertices[1].position == (yr::math::Vec3{1.0F, 0.0F, 0.0F}));
  CHECK(primitive.mesh->vertices[2].position == (yr::math::Vec3{0.0F, 1.0F, 0.0F}));

  CHECK(primitive.material->baseColorFactor == (yr::math::Vec3{1.0F, 1.0F, 1.0F}));
  CHECK(primitive.material->metallic == 1.0F);
  CHECK(primitive.material->roughness == 1.0F);
  CHECK(primitive.material->alphaMode == AlphaMode::kOpaque);
  CHECK(primitive.material->alphaCutoff == 0.5F);
  CHECK_FALSE(primitive.material->baseColorTexture.valid());
}

TEST_CASE("gltf loader: invalid paths and malformed files fail cleanly", "[asset][gltf]") {
  SECTION("non-res path") {
    const GltfLoadResult result = yr::asset::loadGltfScene("models/ToyCar/ToyCar.gltf", YR_TEST_ASSET_ROOT);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
  }
  SECTION("missing file") {
    const GltfLoadResult result = yr::asset::loadGltfScene("res://models/NoSuchFile.gltf", YR_TEST_ASSET_ROOT);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
  }
  SECTION("broken JSON") {
    const GltfLoadResult result = yr::asset::loadGltfScene("res://broken.gltf", YR_TEST_FIXTURE_ROOT);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
  }
  SECTION("required extension") {
    const GltfLoadResult result = yr::asset::loadGltfScene("res://required_extension.gltf", YR_TEST_FIXTURE_ROOT);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
    CHECK(result.error.find("KHR_draco_mesh_compression") != std::string::npos);
  }
}

TEST_CASE("gltf loader: ToyCar geometry, materials, textures and warnings", "[asset][gltf][toycar]") {
  const GltfLoadResult result = yr::asset::loadGltfScene("res://models/ToyCar/ToyCar.gltf", YR_TEST_ASSET_ROOT);
  CAPTURE(result.error);
  REQUIRE(result.ok);

  REQUIRE(result.scene.roots.size() == 11);
  const auto& bodyTransform = result.scene.roots[0].localTransform;
  CHECK(bodyTransform.scale.x == Catch::Approx(0.0001F));
  CHECK(bodyTransform.scale.y == Catch::Approx(0.0001F));
  CHECK(bodyTransform.scale.z == Catch::Approx(0.0001F));
  CHECK(bodyTransform.rotation != yr::math::Quat::identity());
  const auto& cameraTransform = result.scene.roots[3].localTransform;
  CHECK(cameraTransform.position.x == Catch::Approx(-0.0169006381F));
  CHECK(cameraTransform.position.y == Catch::Approx(0.0253599286F));
  CHECK(cameraTransform.position.z == Catch::Approx(0.0302319955F));
  std::size_t totalPrimitives = 0;
  for (const GltfNode& root : result.scene.roots) {
    totalPrimitives += countPrimitives(root);
  }
  CHECK(totalPrimitives == 3);
  for (const GltfNode& root : result.scene.roots) {
    checkNodeResources(root);
  }

  REQUIRE(result.scene.roots[0].primitives.size() == 1);
  REQUIRE(result.scene.roots[1].primitives.size() == 1);
  REQUIRE(result.scene.roots[2].primitives.size() == 1);

  const auto& mesh0 = result.scene.roots[0].primitives[0].mesh;
  const auto& mesh1 = result.scene.roots[1].primitives[0].mesh;
  const auto& mesh2 = result.scene.roots[2].primitives[0].mesh;
  REQUIRE(mesh0.valid());
  REQUIRE(mesh1.valid());
  REQUIRE(mesh2.valid());
  CHECK(mesh0->vertices.size() == 66951);
  CHECK(mesh0->indices.size() == 266511);
  CHECK(mesh1->vertices.size() == 8959);
  CHECK(mesh1->indices.size() == 52815);
  CHECK(mesh2->vertices.size() == 1519);
  CHECK(mesh2->indices.size() == 7482);

  const auto& material0 = result.scene.roots[0].primitives[0].material;
  const auto& material1 = result.scene.roots[1].primitives[0].material;
  const auto& material2 = result.scene.roots[2].primitives[0].material;
  REQUIRE(material0.valid());
  REQUIRE(material1.valid());
  REQUIRE(material2.valid());

  CHECK(material0->doubleSided);
  CHECK(material0->alphaMode == AlphaMode::kOpaque);
  REQUIRE(material0->baseColorTexture.valid());
  REQUIRE(material0->normalTexture.valid());
  REQUIRE(material0->metallicRoughnessTexture.valid());
  REQUIRE(material0->emissiveTexture.valid());
  CHECK(material0->baseColorTexture->width == 1024);
  CHECK(material0->baseColorTexture->height == 1024);
  CHECK(material0->normalTexture->width == 1024);
  CHECK(material0->metallicRoughnessTexture->width == 1024);
  CHECK(material0->emissiveTexture->width == 1024);

  // ToyCar.gltf 的 Fabric 材质实际含 baseColorTexture/normalTexture（512x512）。
  CHECK(material1->baseColorFactor.x == Catch::Approx(0.15F));
  CHECK(material1->baseColorFactor.y == Catch::Approx(0.15F));
  CHECK(material1->baseColorFactor.z == Catch::Approx(0.15F));
  CHECK(material1->metallic == 0.0F);
  CHECK(material1->roughness == 1.0F);
  REQUIRE(material1->baseColorTexture.valid());
  REQUIRE(material1->normalTexture.valid());
  CHECK(material1->baseColorTexture->width == 512);
  CHECK(material1->normalTexture->width == 512);

  CHECK_FALSE(material2->baseColorTexture.valid());
  CHECK_FALSE(material2->normalTexture.valid());
  CHECK_FALSE(material2->metallicRoughnessTexture.valid());
  CHECK_FALSE(material2->emissiveTexture.valid());
  CHECK(material2->baseColorFactor.x == Catch::Approx(0.3F));
  CHECK(material2->baseColorFactor.y == Catch::Approx(0.8F));
  CHECK(material2->baseColorFactor.z == Catch::Approx(0.3F));
  CHECK(material2->metallic == 0.0F);
  CHECK(material2->roughness == 0.0F);

  CHECK_FALSE(result.warnings.empty());
  CHECK(std::any_of(result.warnings.begin(), result.warnings.end(), [](const std::string& warning) {
    return warning.find("unsupported extension") != std::string::npos;
  }));

  const auto& root0 = result.scene.roots[0].primitives[0];
  CHECK(root0.mesh->state() == LoadState::kReady);
  CHECK(root0.material->state() == LoadState::kReady);
  CHECK(root0.mesh->uid().valid());
  CHECK(root0.material->uid().valid());
  CHECK(material0->baseColorTexture->state() == LoadState::kReady);
  CHECK(material0->normalTexture->state() == LoadState::kReady);
  CHECK(material0->metallicRoughnessTexture->state() == LoadState::kReady);
  CHECK(material0->emissiveTexture->state() == LoadState::kReady);
  CHECK(material1->baseColorTexture->state() == LoadState::kReady);
  CHECK(material1->normalTexture->state() == LoadState::kReady);
}

TEST_CASE("gltf loader: deferTextures registers kLoading placeholders and pending requests",
          "[asset][gltf][toycar][defer]") {
  const yr::asset::GltfLoadOptions options{.deferTextures = true};
  const GltfLoadResult result =
      yr::asset::loadGltfScene("res://models/ToyCar/ToyCar.gltf", YR_TEST_ASSET_ROOT, nullptr, &options);
  CAPTURE(result.error);
  REQUIRE(result.ok);

  REQUIRE_FALSE(result.pendingTextures.empty());
  for (const auto& request : result.pendingTextures) {
    CHECK(request.uid.valid());
    CHECK(request.resPath.starts_with("res://models/ToyCar/"));
  }

  REQUIRE_FALSE(result.scene.roots.empty());
  REQUIRE_FALSE(result.scene.roots[0].primitives.empty());
  const auto& primitive = result.scene.roots[0].primitives[0];
  REQUIRE(primitive.mesh.valid());
  REQUIRE(primitive.material.valid());
  CHECK(primitive.mesh->state() == LoadState::kReady);
  CHECK(primitive.material->state() == LoadState::kReady);

  const auto& baseColorTexture = primitive.material->baseColorTexture;
  REQUIRE(baseColorTexture.valid());
  CHECK(baseColorTexture->state() == LoadState::kLoading);
  CHECK(baseColorTexture->uid().valid());
  CHECK(baseColorTexture->width == 0);
  CHECK(baseColorTexture->height == 0);
  CHECK(baseColorTexture->rgba8.empty());
  CHECK(baseColorTexture->sourcePath().starts_with("res://models/ToyCar/"));

  const auto pending = std::find_if(
      result.pendingTextures.begin(), result.pendingTextures.end(),
      [&](const yr::asset::GltfTextureRequest& request) { return request.uid == baseColorTexture->uid(); });
  REQUIRE(pending != result.pendingTextures.end());
  CHECK(pending->resPath == baseColorTexture->sourcePath());
}

TEST_CASE("gltf loader: ToyCar resource UIDs are deterministic", "[asset][gltf][toycar]") {
  const GltfLoadResult first = yr::asset::loadGltfScene("res://models/ToyCar/ToyCar.gltf", YR_TEST_ASSET_ROOT);
  REQUIRE(first.ok);
  const auto& firstPrimitive = first.scene.roots[0].primitives[0];
  const auto firstMeshUid = firstPrimitive.mesh->uid();
  const auto firstTextureUid = firstPrimitive.material->baseColorTexture->uid();

  const GltfLoadResult second = yr::asset::loadGltfScene("res://models/ToyCar/ToyCar.gltf", YR_TEST_ASSET_ROOT);
  REQUIRE(second.ok);
  const auto& secondPrimitive = second.scene.roots[0].primitives[0];
  CHECK(secondPrimitive.mesh->uid() == firstMeshUid);
  CHECK(secondPrimitive.material->baseColorTexture->uid() == firstTextureUid);
}

TEST_CASE("gltf loader: resource hooks reuse cached objects", "[asset][gltf][hooks]") {
  std::unordered_map<AssetUID, yr::obj::Ref<Resource>> cache;
  const GltfResourceHooks hooks{
      .find = [&cache](AssetUID uid) -> yr::obj::Ref<Resource> {
        const auto it = cache.find(uid);
        return it != cache.end() ? it->second : yr::obj::Ref<Resource>{};
      },
      .store =
          [&cache](yr::obj::Ref<Resource> resource) {
            if (!resource || !resource->uid().valid()) {
              return false;
            }
            const AssetUID uid = resource->uid();
            return cache.emplace(uid, std::move(resource)).second;
          },
  };

  const GltfLoadResult first = yr::asset::loadGltfScene("res://triangle.gltf", YR_TEST_FIXTURE_ROOT, &hooks);
  CAPTURE(first.error);
  REQUIRE(first.ok);
  REQUIRE_FALSE(cache.empty());
  REQUIRE_FALSE(first.scene.roots.empty());
  REQUIRE_FALSE(first.scene.roots[0].primitives.empty());

  const Mesh* const firstMesh = first.scene.roots[0].primitives[0].mesh.get();
  const Material* const firstMaterial = first.scene.roots[0].primitives[0].material.get();
  REQUIRE(firstMesh != nullptr);
  REQUIRE(firstMaterial != nullptr);

  const GltfLoadResult second = yr::asset::loadGltfScene("res://triangle.gltf", YR_TEST_FIXTURE_ROOT, &hooks);
  CAPTURE(second.error);
  REQUIRE(second.ok);
  REQUIRE_FALSE(second.scene.roots.empty());
  REQUIRE_FALSE(second.scene.roots[0].primitives.empty());
  CHECK(second.scene.roots[0].primitives[0].mesh.get() == firstMesh);
  CHECK(second.scene.roots[0].primitives[0].material.get() == firstMaterial);
}
