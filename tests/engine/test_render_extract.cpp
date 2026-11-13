#include <catch2/catch_test_macros.hpp>

#include <yr/asset/resource_types.h>
#include <yr/engine/render_extract.h>
#include <yr/object/ref.h>
#include <yr/render/snapshot.h>
#include <yr/scene/light_node.h>
#include <yr/scene/mesh_instance_node.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

using yr::asset::AlphaMode;
using yr::asset::AssetUID;
using yr::asset::Material;
using yr::asset::Mesh;
using yr::asset::MeshVertex;
using yr::asset::Texture;
using yr::asset::TextureColorSpace;
using yr::engine::RenderExtractSystem;
using yr::math::Mat4;
using yr::math::Vec3;
using yr::obj::Ref;
using yr::render::CameraPod;
using yr::render::MaterialAlphaMode;
using yr::render::RenderSnapshot;
using yr::scene::LightNode;
using yr::scene::LightType;
using yr::scene::MeshInstanceNode;
using yr::scene::Node;
using yr::scene::SceneTree;

namespace {

  Ref<Mesh> makeMesh(std::uint64_t uid) {
    Ref<Mesh> mesh = yr::obj::make_ref<Mesh>();
    mesh->setUid(AssetUID{uid});
    REQUIRE(mesh->markLoading());
    REQUIRE(mesh->markReady());
    mesh->vertices.push_back(MeshVertex{Vec3{1.0F, 2.0F, 3.0F}, Vec3{0.0F, 1.0F, 0.0F}, 0.25F, 0.75F});
    mesh->indices = {0, 0, 0};
    return mesh;
  }

  Ref<Texture> makeTexture(std::uint64_t uid) {
    Ref<Texture> texture = yr::obj::make_ref<Texture>();
    texture->setUid(AssetUID{uid});
    REQUIRE(texture->markLoading());
    REQUIRE(texture->markReady());
    texture->width = 1;
    texture->height = 1;
    texture->rgba8 = {10, 20, 30, 40};
    return texture;
  }

  Ref<Texture> makeLoadingTexture(std::uint64_t uid) {
    Ref<Texture> texture = yr::obj::make_ref<Texture>();
    texture->setUid(AssetUID{uid});
    REQUIRE(texture->markLoading());
    return texture;
  }

  Ref<Material> makeMaterial(std::uint64_t uid, const Ref<Texture>& texture) {
    Ref<Material> material = yr::obj::make_ref<Material>();
    material->setUid(AssetUID{uid});
    REQUIRE(material->markLoading());
    material->baseColorTexture = texture;
    material->baseColorFactor = Vec3{0.2F, 0.3F, 0.4F};
    material->baseColorAlpha = 0.5F;
    material->metallic = 0.6F;
    material->roughness = 0.7F;
    REQUIRE(material->markReady());
    return material;
  }

  Ref<Material> makeFullMaterial(std::uint64_t uid, const Ref<Texture>& baseColor, const Ref<Texture>& normal,
                                 const Ref<Texture>& metallicRoughness, const Ref<Texture>& emissive) {
    Ref<Material> material = makeMaterial(uid, baseColor);
    material->normalTexture = normal;
    material->metallicRoughnessTexture = metallicRoughness;
    material->emissiveTexture = emissive;
    material->normalScale = 0.75F;
    material->emissive = Vec3{0.1F, 0.2F, 0.3F};
    material->alphaCutoff = 0.35F;
    return material;
  }

  bool worldMatches(const yr::render::TransformPod& world, const Mat4& global) {
    for (std::size_t index = 0; index < 16; ++index) {
      if (world.m[index] != global.m_[index]) {
        return false;
      }
    }
    return true;
  }

} // namespace

TEST_CASE("render_extract: only ready MeshInstanceNodes emit preorder items and resource deltas",
          "[engine][render][extract]") {
  SceneTree tree;
  auto root = std::make_unique<Node>();
  auto first = std::make_unique<MeshInstanceNode>();
  auto sharedMesh = std::make_unique<MeshInstanceNode>();
  auto secondMesh = std::make_unique<MeshInstanceNode>();
  MeshInstanceNode* const rawFirst = first.get();
  MeshInstanceNode* const rawShared = sharedMesh.get();
  MeshInstanceNode* const rawSecond = secondMesh.get();

  Ref<Mesh> mesh20 = makeMesh(20);
  Ref<Mesh> mesh10 = makeMesh(10);
  Ref<Texture> texture = makeTexture(30);
  Ref<Material> material = makeMaterial(40, texture);
  Ref<Texture> secondTexture = makeTexture(15);
  Ref<Material> secondMaterial = makeMaterial(5, secondTexture);

  rawFirst->setMesh(mesh20);
  rawFirst->setMaterial(material);
  rawShared->setMesh(mesh20);
  rawShared->setMaterial(material);
  rawSecond->setMesh(mesh10);
  rawSecond->setMaterial(secondMaterial);
  rawFirst->setPosition(Vec3{1.0F, 0.0F, 0.0F});
  rawShared->setPosition(Vec3{0.0F, 2.0F, 0.0F});
  rawSecond->setPosition(Vec3{0.0F, 0.0F, 3.0F});

  REQUIRE(rawFirst->addChild(std::move(sharedMesh)));
  REQUIRE(root->addChild(std::move(first)));
  REQUIRE(root->addChild(std::move(secondMesh)));
  REQUIRE(tree.setRoot(std::move(root)));
  tree.refreshTransforms();

  const std::uint32_t mesh20Refs = mesh20->refcount();
  const std::uint32_t mesh10Refs = mesh10->refcount();
  const std::uint32_t materialRefs = material->refcount();
  const std::uint32_t textureRefs = texture->refcount();
  const std::uint32_t secondMaterialRefs = secondMaterial->refcount();
  const std::uint32_t secondTextureRefs = secondTexture->refcount();

  CameraPod camera;
  camera.eye[0] = 7.0F;
  camera.eye[1] = -8.0F;
  camera.eye[2] = 9.0F;
  RenderSnapshot snapshot;
  snapshot.frame_index = 17;
  RenderExtractSystem extractor;
  extractor.extract(tree, camera, snapshot);

  REQUIRE(snapshot.items.size() == 3);
  CHECK(snapshot.frame_index == 17);
  CHECK(snapshot.items[0].object.value == rawFirst->id().raw());
  CHECK(snapshot.items[1].object.value == rawShared->id().raw());
  CHECK(snapshot.items[2].object.value == rawSecond->id().raw());
  CHECK(worldMatches(snapshot.items[0].world, rawFirst->globalTransform()));
  CHECK(worldMatches(snapshot.items[1].world, rawShared->globalTransform()));
  CHECK(worldMatches(snapshot.items[2].world, rawSecond->globalTransform()));
  CHECK(snapshot.items[0].mesh.value == 20);
  CHECK(snapshot.items[1].mesh.value == 20);
  CHECK(snapshot.items[2].mesh.value == 10);
  CHECK(snapshot.items[0].material.value == 40);
  CHECK(snapshot.items[0].world.m[12] == rawFirst->globalTransform()[12]);
  CHECK(snapshot.camera.eye[0] == camera.eye[0]);
  CHECK(snapshot.camera.eye[1] == camera.eye[1]);
  CHECK(snapshot.camera.eye[2] == camera.eye[2]);

  REQUIRE(snapshot.uploads.meshes.size() == 2);
  CHECK(snapshot.uploads.meshes[0].mesh.value == 20);
  CHECK(snapshot.uploads.meshes[1].mesh.value == 10);
  CHECK(snapshot.uploads.meshes[0].vertices.size() == 1);
  CHECK(snapshot.uploads.meshes[0].vertices[0].position[1] == 2.0F);
  CHECK(snapshot.uploads.meshes[0].vertices[0].normal[1] == 1.0F);
  CHECK(snapshot.uploads.meshes[0].vertices[0].uv[1] == 0.75F);
  REQUIRE(snapshot.uploads.materials.size() == 2);
  CHECK(snapshot.uploads.materials[0].material.value == 40);
  CHECK(snapshot.uploads.materials[0].baseColorTexture.value == 30);
  CHECK(snapshot.uploads.materials[0].baseColorFactor[3] == 0.5F);
  CHECK(snapshot.uploads.materials[0].metallic == 0.6F);
  CHECK(snapshot.uploads.materials[0].roughness == 0.7F);
  CHECK(snapshot.uploads.materials[1].material.value == 5);
  CHECK(snapshot.uploads.materials[1].baseColorTexture.value == 15);
  REQUIRE(snapshot.uploads.textures.size() == 2);
  CHECK(snapshot.uploads.textures[0].texture.value == 30);
  CHECK(snapshot.uploads.textures[1].texture.value == 15);
  CHECK(snapshot.uploads.textures[0].rgba8 == texture->rgba8);
  CHECK(snapshot.releases.meshes.empty());
  CHECK(snapshot.releases.materials.empty());
  CHECK(snapshot.releases.textures.empty());
  CHECK(mesh20->refcount() == mesh20Refs);
  CHECK(mesh10->refcount() == mesh10Refs);
  CHECK(material->refcount() == materialRefs);
  CHECK(texture->refcount() == textureRefs);
  CHECK(secondMaterial->refcount() == secondMaterialRefs);
  CHECK(secondTexture->refcount() == secondTextureRefs);

  extractor.extract(tree, camera, snapshot);
  CHECK(snapshot.items.size() == 3);
  CHECK(snapshot.uploads.meshes.empty());
  CHECK(snapshot.uploads.materials.empty());
  CHECK(snapshot.uploads.textures.empty());
  CHECK(snapshot.releases.meshes.empty());
  CHECK(snapshot.releases.materials.empty());
  CHECK(snapshot.releases.textures.empty());
  CHECK(mesh20->refcount() == mesh20Refs);
  CHECK(mesh10->refcount() == mesh10Refs);
  CHECK(material->refcount() == materialRefs);
  CHECK(texture->refcount() == textureRefs);
  CHECK(secondMaterial->refcount() == secondMaterialRefs);
  CHECK(secondTexture->refcount() == secondTextureRefs);

  { std::unique_ptr<Node> removed = tree.clearRoot(); }
  extractor.extract(tree, camera, snapshot);
  CHECK(snapshot.items.empty());
  REQUIRE(snapshot.releases.meshes.size() == 2);
  CHECK(snapshot.releases.meshes[0].value == 10);
  CHECK(snapshot.releases.meshes[1].value == 20);
  REQUIRE(snapshot.releases.materials.size() == 2);
  CHECK(snapshot.releases.materials[0].value == 5);
  CHECK(snapshot.releases.materials[1].value == 40);
  REQUIRE(snapshot.releases.textures.size() == 2);
  CHECK(snapshot.releases.textures[0].value == 15);
  CHECK(snapshot.releases.textures[1].value == 30);
}

TEST_CASE("render_extract: ordinary nodes, invalid resources, and empty trees are safe", "[engine][render][extract]") {
  SceneTree tree;
  auto root = std::make_unique<Node>();
  auto invalid = std::make_unique<MeshInstanceNode>();
  REQUIRE(root->addChild(std::move(invalid)));
  REQUIRE(tree.setRoot(std::move(root)));

  RenderSnapshot snapshot;
  snapshot.uploads.meshes.push_back({});
  snapshot.releases.meshes.push_back({1});
  RenderExtractSystem extractor;
  extractor.extract(tree, CameraPod{}, snapshot);

  CHECK(snapshot.items.empty());
  CHECK(snapshot.lightCount == 0);
  CHECK(snapshot.uploads.meshes.empty());
  CHECK(snapshot.uploads.materials.empty());
  CHECK(snapshot.uploads.textures.empty());
  CHECK(snapshot.releases.meshes.empty());
  CHECK(snapshot.releases.materials.empty());
  CHECK(snapshot.releases.textures.empty());

  SceneTree emptyTree;
  extractor.extract(emptyTree, CameraPod{}, snapshot);
  CHECK(snapshot.items.empty());
  CHECK(snapshot.lightCount == 0);
  CHECK(snapshot.uploads.meshes.empty());
  CHECK(snapshot.uploads.materials.empty());
  CHECK(snapshot.uploads.textures.empty());
  CHECK(snapshot.releases.meshes.empty());
  CHECK(snapshot.releases.materials.empty());
  CHECK(snapshot.releases.textures.empty());
}

TEST_CASE("render_extract: loading base color texture skips the mesh instance", "[engine][render][extract][async]") {
  SceneTree tree;
  auto root = std::make_unique<MeshInstanceNode>();
  MeshInstanceNode* const instance = root.get();
  Ref<Mesh> mesh = makeMesh(1);
  Ref<Texture> texture = makeLoadingTexture(3);
  Ref<Material> material = makeMaterial(2, texture);
  instance->setMesh(mesh);
  instance->setMaterial(material);
  REQUIRE(tree.setRoot(std::move(root)));

  RenderSnapshot snapshot;
  RenderExtractSystem extractor;
  extractor.extract(tree, CameraPod{}, snapshot);

  CHECK(snapshot.items.empty());
  CHECK(snapshot.uploads.meshes.empty());
  CHECK(snapshot.uploads.materials.empty());
  CHECK(snapshot.uploads.textures.empty());
  CHECK(snapshot.releases.meshes.empty());
  CHECK(snapshot.releases.materials.empty());
  CHECK(snapshot.releases.textures.empty());

  texture->width = 1;
  texture->height = 1;
  texture->rgba8 = {1, 2, 3, 4};
  REQUIRE(texture->markReady());

  extractor.extract(tree, CameraPod{}, snapshot);

  REQUIRE(snapshot.items.size() == 1);
  REQUIRE(snapshot.uploads.meshes.size() == 1);
  REQUIRE(snapshot.uploads.materials.size() == 1);
  CHECK(snapshot.uploads.materials[0].baseColorTexture.value == 3);
  REQUIRE(snapshot.uploads.textures.size() == 1);
  CHECK(snapshot.uploads.textures[0].texture.value == 3);
}

TEST_CASE("render_extract: material without base color texture still emits an item",
          "[engine][render][extract][async]") {
  SceneTree tree;
  auto root = std::make_unique<MeshInstanceNode>();
  MeshInstanceNode* const instance = root.get();
  Ref<Mesh> mesh = makeMesh(1);
  Ref<Material> material = makeMaterial(2, {});
  instance->setMesh(mesh);
  instance->setMaterial(material);
  REQUIRE(tree.setRoot(std::move(root)));

  RenderSnapshot snapshot;
  RenderExtractSystem extractor;
  extractor.extract(tree, CameraPod{}, snapshot);

  REQUIRE(snapshot.items.size() == 1);
  CHECK(snapshot.items[0].material.value == 2);
  REQUIRE(snapshot.uploads.materials.size() == 1);
  CHECK(snapshot.uploads.materials[0].baseColorTexture.value == 0);
  CHECK(snapshot.uploads.textures.empty());
}

TEST_CASE("render_extract: malformed texture skips the mesh instance", "[engine][render][extract]") {
  SceneTree tree;
  auto root = std::make_unique<MeshInstanceNode>();
  MeshInstanceNode* const instance = root.get();
  Ref<Mesh> mesh = makeMesh(1);
  Ref<Texture> texture = makeTexture(3);
  texture->width = 2;
  Ref<Material> material = makeMaterial(2, texture);
  instance->setMesh(mesh);
  instance->setMaterial(material);
  REQUIRE(tree.setRoot(std::move(root)));

  RenderSnapshot snapshot;
  RenderExtractSystem extractor;
  extractor.extract(tree, CameraPod{}, snapshot);

  CHECK(snapshot.items.empty());
  CHECK(snapshot.uploads.textures.empty());
  CHECK(snapshot.uploads.materials.empty());
  CHECK(snapshot.releases.materials.empty());
  CHECK(snapshot.releases.textures.empty());
}

TEST_CASE("render_extract: four ready material textures upload with matching color spaces",
          "[engine][render][extract]") {
  SceneTree tree;
  auto root = std::make_unique<MeshInstanceNode>();
  MeshInstanceNode* const instance = root.get();
  Ref<Mesh> mesh = makeMesh(1);
  Ref<Texture> baseColor = makeTexture(10);
  baseColor->colorSpace = TextureColorSpace::kSrgb;
  Ref<Texture> normal = makeTexture(11);
  normal->colorSpace = TextureColorSpace::kLinear;
  Ref<Texture> metallicRoughness = makeTexture(12);
  metallicRoughness->colorSpace = TextureColorSpace::kLinear;
  Ref<Texture> emissive = makeTexture(13);
  emissive->colorSpace = TextureColorSpace::kSrgb;
  Ref<Material> material = makeFullMaterial(20, baseColor, normal, metallicRoughness, emissive);
  instance->setMesh(mesh);
  instance->setMaterial(material);
  REQUIRE(tree.setRoot(std::move(root)));

  RenderSnapshot snapshot;
  RenderExtractSystem extractor;
  extractor.extract(tree, CameraPod{}, snapshot);

  REQUIRE(snapshot.items.size() == 1);
  REQUIRE(snapshot.uploads.textures.size() == 4);
  CHECK(snapshot.uploads.textures[0].texture.value == 10);
  CHECK(snapshot.uploads.textures[0].colorSpace == yr::render::TextureColorSpace::kSrgb);
  CHECK(snapshot.uploads.textures[1].texture.value == 11);
  CHECK(snapshot.uploads.textures[1].colorSpace == yr::render::TextureColorSpace::kLinear);
  CHECK(snapshot.uploads.textures[2].texture.value == 12);
  CHECK(snapshot.uploads.textures[2].colorSpace == yr::render::TextureColorSpace::kLinear);
  CHECK(snapshot.uploads.textures[3].texture.value == 13);
  CHECK(snapshot.uploads.textures[3].colorSpace == yr::render::TextureColorSpace::kSrgb);

  REQUIRE(snapshot.uploads.materials.size() == 1);
  const yr::render::MaterialUpload& upload = snapshot.uploads.materials[0];
  CHECK(upload.baseColorTexture.value == 10);
  CHECK(upload.normalTexture.value == 11);
  CHECK(upload.metallicRoughnessTexture.value == 12);
  CHECK(upload.emissiveTexture.value == 13);
  CHECK(upload.normalScale == 0.75F);
  CHECK(upload.emissiveFactor[0] == 0.1F);
  CHECK(upload.emissiveFactor[1] == 0.2F);
  CHECK(upload.emissiveFactor[2] == 0.3F);
  CHECK(upload.alphaCutoff == 0.35F);
  CHECK(upload.alphaMode == MaterialAlphaMode::kOpaque);
}

TEST_CASE("render_extract: any loading material texture skips the mesh instance", "[engine][render][extract]") {
  SceneTree tree;
  auto root = std::make_unique<MeshInstanceNode>();
  MeshInstanceNode* const instance = root.get();
  Ref<Mesh> mesh = makeMesh(1);
  Ref<Texture> baseColor = makeTexture(10);
  Ref<Texture> normal = makeTexture(11);
  Ref<Texture> metallicRoughness = makeLoadingTexture(12);
  Ref<Texture> emissive = makeTexture(13);
  Ref<Material> material = makeFullMaterial(20, baseColor, normal, metallicRoughness, emissive);
  instance->setMesh(mesh);
  instance->setMaterial(material);
  REQUIRE(tree.setRoot(std::move(root)));

  RenderSnapshot snapshot;
  RenderExtractSystem extractor;
  extractor.extract(tree, CameraPod{}, snapshot);

  CHECK(snapshot.items.empty());
  CHECK(snapshot.uploads.meshes.empty());
  CHECK(snapshot.uploads.textures.empty());
  CHECK(snapshot.uploads.materials.empty());

  metallicRoughness->width = 1;
  metallicRoughness->height = 1;
  metallicRoughness->rgba8 = {1, 2, 3, 4};
  REQUIRE(metallicRoughness->markReady());

  extractor.extract(tree, CameraPod{}, snapshot);

  REQUIRE(snapshot.items.size() == 1);
  REQUIRE(snapshot.uploads.textures.size() == 4);
}

TEST_CASE("render_extract: mask alpha mode maps to mask and blend maps to opaque", "[engine][render][extract]") {
  SceneTree tree;
  auto root = std::make_unique<Node>();
  auto maskedNode = std::make_unique<MeshInstanceNode>();
  auto blendedNode = std::make_unique<MeshInstanceNode>();
  Ref<Mesh> mesh = makeMesh(1);
  Ref<Material> masked = makeMaterial(30, {});
  masked->alphaMode = AlphaMode::kMask;
  Ref<Material> blended = makeMaterial(31, {});
  blended->alphaMode = AlphaMode::kBlend;
  maskedNode->setMesh(mesh);
  maskedNode->setMaterial(masked);
  blendedNode->setMesh(mesh);
  blendedNode->setMaterial(blended);
  REQUIRE(root->addChild(std::move(maskedNode)));
  REQUIRE(root->addChild(std::move(blendedNode)));
  REQUIRE(tree.setRoot(std::move(root)));

  RenderSnapshot snapshot;
  RenderExtractSystem extractor;
  extractor.extract(tree, CameraPod{}, snapshot);

  REQUIRE(snapshot.uploads.materials.size() == 2);
  CHECK(snapshot.uploads.materials[0].material.value == 30);
  CHECK(snapshot.uploads.materials[0].alphaMode == MaterialAlphaMode::kMask);
  CHECK(snapshot.uploads.materials[1].material.value == 31);
  CHECK(snapshot.uploads.materials[1].alphaMode == MaterialAlphaMode::kOpaque);
}

TEST_CASE("render_extract: releasing a full material releases all four textures", "[engine][render][extract]") {
  SceneTree tree;
  auto root = std::make_unique<MeshInstanceNode>();
  MeshInstanceNode* const instance = root.get();
  Ref<Mesh> mesh = makeMesh(1);
  Ref<Texture> baseColor = makeTexture(10);
  Ref<Texture> normal = makeTexture(11);
  Ref<Texture> metallicRoughness = makeTexture(12);
  Ref<Texture> emissive = makeTexture(13);
  Ref<Material> material = makeFullMaterial(20, baseColor, normal, metallicRoughness, emissive);
  instance->setMesh(mesh);
  instance->setMaterial(material);
  REQUIRE(tree.setRoot(std::move(root)));

  RenderSnapshot snapshot;
  RenderExtractSystem extractor;
  extractor.extract(tree, CameraPod{}, snapshot);
  REQUIRE(snapshot.uploads.textures.size() == 4);

  { std::unique_ptr<Node> removed = tree.clearRoot(); }
  extractor.extract(tree, CameraPod{}, snapshot);

  CHECK(snapshot.items.empty());
  std::vector<std::uint64_t> released;
  released.reserve(snapshot.releases.textures.size());
  for (const yr::render::AssetHandle& handle : snapshot.releases.textures) {
    released.push_back(handle.value);
  }
  std::sort(released.begin(), released.end());
  CHECK(released == std::vector<std::uint64_t>{10, 11, 12, 13});
}

TEST_CASE("render_extract: collects lights in preorder from global transforms", "[engine][render][extract][light]") {
  SceneTree tree;
  auto root = std::make_unique<Node>();
  auto directional = std::make_unique<LightNode>();
  auto point = std::make_unique<LightNode>();
  auto child = std::make_unique<LightNode>();
  LightNode* const rawDirectional = directional.get();
  LightNode* const rawPoint = point.get();
  LightNode* const rawChild = child.get();

  rawDirectional->setType(LightType::kDirectional);
  rawDirectional->setPosition(Vec3{1.0F, 2.0F, 3.0F});
  rawDirectional->setColor(Vec3{0.25F, 0.5F, 0.75F});
  rawDirectional->setIntensity(2.0F);
  rawDirectional->setRange(30.0F);
  rawDirectional->setDirection(Vec3{0.0F, -4.0F, 0.0F});

  rawPoint->setType(LightType::kPoint);
  rawPoint->setPosition(Vec3{4.0F, 5.0F, 6.0F});
  rawPoint->setColor(Vec3{1.0F, 0.0F, 0.0F});
  rawPoint->setIntensity(5.0F);
  rawPoint->setRange(7.0F);
  rawPoint->setDirection(Vec3{2.0F, 0.0F, 0.0F});

  rawChild->setType(LightType::kPoint);
  rawChild->setPosition(Vec3{1.0F, 1.0F, 1.0F});
  rawChild->setColor(Vec3{0.0F, 1.0F, 0.0F});
  rawChild->setIntensity(0.5F);
  rawChild->setRange(2.0F);

  REQUIRE(rawPoint->addChild(std::move(child)));
  REQUIRE(root->addChild(std::move(directional)));
  REQUIRE(root->addChild(std::move(point)));
  REQUIRE(tree.setRoot(std::move(root)));
  tree.refreshTransforms();

  RenderSnapshot snapshot;
  RenderExtractSystem extractor;
  extractor.extract(tree, CameraPod{}, snapshot);

  REQUIRE(snapshot.lightCount == 3);

  CHECK(snapshot.lights[0].type == yr::render::LightType::kDirectional);
  CHECK(snapshot.lights[0].position[0] == 1.0F);
  CHECK(snapshot.lights[0].position[1] == 2.0F);
  CHECK(snapshot.lights[0].position[2] == 3.0F);
  CHECK(snapshot.lights[0].position[0] == rawDirectional->globalTransform()[12]);
  CHECK(snapshot.lights[0].position[1] == rawDirectional->globalTransform()[13]);
  CHECK(snapshot.lights[0].position[2] == rawDirectional->globalTransform()[14]);
  CHECK(snapshot.lights[0].direction[0] == 0.0F);
  CHECK(snapshot.lights[0].direction[1] == -1.0F);
  CHECK(snapshot.lights[0].direction[2] == 0.0F);
  CHECK(snapshot.lights[0].color[0] == 0.25F);
  CHECK(snapshot.lights[0].color[1] == 0.5F);
  CHECK(snapshot.lights[0].color[2] == 0.75F);
  CHECK(snapshot.lights[0].intensity == 2.0F);
  CHECK(snapshot.lights[0].range == 30.0F);

  CHECK(snapshot.lights[1].type == yr::render::LightType::kPoint);
  CHECK(snapshot.lights[1].position[0] == 4.0F);
  CHECK(snapshot.lights[1].position[1] == 5.0F);
  CHECK(snapshot.lights[1].position[2] == 6.0F);
  CHECK(snapshot.lights[1].position[0] == rawPoint->globalTransform()[12]);
  CHECK(snapshot.lights[1].position[1] == rawPoint->globalTransform()[13]);
  CHECK(snapshot.lights[1].position[2] == rawPoint->globalTransform()[14]);
  CHECK(snapshot.lights[1].direction[0] == 1.0F);
  CHECK(snapshot.lights[1].direction[1] == 0.0F);
  CHECK(snapshot.lights[1].direction[2] == 0.0F);
  CHECK(snapshot.lights[1].color[0] == 1.0F);
  CHECK(snapshot.lights[1].color[1] == 0.0F);
  CHECK(snapshot.lights[1].color[2] == 0.0F);
  CHECK(snapshot.lights[1].intensity == 5.0F);
  CHECK(snapshot.lights[1].range == 7.0F);

  CHECK(snapshot.lights[2].type == yr::render::LightType::kPoint);
  CHECK(snapshot.lights[2].position[0] == 5.0F);
  CHECK(snapshot.lights[2].position[1] == 6.0F);
  CHECK(snapshot.lights[2].position[2] == 7.0F);
  CHECK(snapshot.lights[2].position[0] == rawChild->globalTransform()[12]);
  CHECK(snapshot.lights[2].position[1] == rawChild->globalTransform()[13]);
  CHECK(snapshot.lights[2].position[2] == rawChild->globalTransform()[14]);
  CHECK(snapshot.lights[2].direction[0] == 0.0F);
  CHECK(snapshot.lights[2].direction[1] == 0.0F);
  CHECK(snapshot.lights[2].direction[2] == -1.0F);
  CHECK(snapshot.lights[2].color[1] == 1.0F);
  CHECK(snapshot.lights[2].intensity == 0.5F);
  CHECK(snapshot.lights[2].range == 2.0F);

  CHECK(snapshot.items.empty());
  CHECK(snapshot.uploads.meshes.empty());
}

TEST_CASE("render_extract: keeps the first eight lights in preorder", "[engine][render][extract][light]") {
  SceneTree tree;
  auto root = std::make_unique<Node>();
  std::vector<LightNode*> lights;
  for (int index = 0; index < 10; ++index) {
    auto light = std::make_unique<LightNode>();
    light->setPosition(Vec3{static_cast<float>(index), 0.0F, 0.0F});
    lights.push_back(light.get());
    REQUIRE(root->addChild(std::move(light)));
  }
  REQUIRE(tree.setRoot(std::move(root)));
  tree.refreshTransforms();

  RenderSnapshot snapshot;
  RenderExtractSystem extractor;
  extractor.extract(tree, CameraPod{}, snapshot);

  REQUIRE(snapshot.lightCount == RenderSnapshot::kMaxLights);
  REQUIRE(lights.size() == 10);
  for (std::size_t index = 0; index < RenderSnapshot::kMaxLights; ++index) {
    CHECK(snapshot.lights[index].position[0] == lights[index]->globalTransform()[12]);
    CHECK(snapshot.lights[index].position[0] == static_cast<float>(index));
  }
}

TEST_CASE("render_extract: lights emit no draw items", "[engine][render][extract][light]") {
  SceneTree tree;
  auto root = std::make_unique<LightNode>();
  root->setType(LightType::kPoint);
  REQUIRE(tree.setRoot(std::move(root)));

  RenderSnapshot snapshot;
  snapshot.items.push_back({});
  RenderExtractSystem extractor;
  extractor.extract(tree, CameraPod{}, snapshot);

  CHECK(snapshot.lightCount == 1);
  CHECK(snapshot.items.empty());
  CHECK(snapshot.uploads.meshes.empty());
  CHECK(snapshot.uploads.materials.empty());
  CHECK(snapshot.uploads.textures.empty());
  CHECK(snapshot.releases.meshes.empty());
  CHECK(snapshot.releases.materials.empty());
  CHECK(snapshot.releases.textures.empty());
}
