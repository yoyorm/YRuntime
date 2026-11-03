#include <catch2/catch_test_macros.hpp>

#include <string>

#include <yr/asset/resource_types.h>
#include <yr/core/assert.h>
#include <yr/object/ref.h>

namespace {

  using yr::asset::AssetUID;
  using yr::asset::LoadState;
  using yr::asset::Material;
  using yr::asset::Mesh;
  using yr::asset::MeshVertex;
  using yr::asset::ResourceKind;
  using yr::asset::Texture;

#if YR_ENABLE_ASSERTS
  yr::core::AssertAction continueAssert(const yr::core::AssertInfo&) noexcept {
    return yr::core::AssertAction::kContinue;
  }

  // 非法状态转换会触发断言；测试里临时改成 continue，作用域结束恢复默认 abort。
  class ContinueAssertScope {
  public:
    ContinueAssertScope() { yr::core::setAssertHandler(continueAssert); }
    ~ContinueAssertScope() { yr::core::setAssertHandler(nullptr); }
  };
#endif

} // namespace

TEST_CASE("resource: fresh resource starts unloaded with no identity", "[asset][resource]") {
  Texture texture;

  CHECK(texture.kind() == ResourceKind::kTexture);
  CHECK(texture.state() == LoadState::kUnloaded);
  CHECK_FALSE(texture.uid().valid());
  CHECK(texture.sourcePath().empty());
  CHECK(texture.error().empty());
  CHECK(texture.width == 0);
  CHECK(texture.height == 0);
  CHECK(texture.rgba8.empty());

  texture.setUid(AssetUID{42});
  texture.setSourcePath("res://textures/test.png");
  CHECK(texture.uid().value == 42);
  CHECK(texture.sourcePath() == "res://textures/test.png");
}

TEST_CASE("resource: loading -> ready clears error", "[asset][resource]") {
  Texture texture;

  CHECK(texture.markLoading());
  CHECK(texture.state() == LoadState::kLoading);

  CHECK(texture.markReady());
  CHECK(texture.state() == LoadState::kReady);
  CHECK(texture.error().empty());
}

TEST_CASE("resource: failed state stores error and can retry loading", "[asset][resource]") {
  Texture texture;

  CHECK(texture.markLoading());
  CHECK(texture.markFailed("decode failed"));
  CHECK(texture.state() == LoadState::kFailed);
  CHECK(texture.error() == "decode failed");

  CHECK(texture.markLoading());
  CHECK(texture.state() == LoadState::kLoading);
  CHECK(texture.error().empty());

  CHECK(texture.markReady());
  CHECK(texture.state() == LoadState::kReady);
}

TEST_CASE("resource: illegal state transitions are rejected", "[asset][resource]") {
#if YR_ENABLE_ASSERTS
  const ContinueAssertScope assertScope;
#endif

  Texture texture;

  CHECK_FALSE(texture.markReady());       // unloaded -> ready
  CHECK_FALSE(texture.markFailed("bad")); // unloaded -> failed
  CHECK(texture.state() == LoadState::kUnloaded);
  CHECK(texture.error().empty());

  CHECK(texture.markLoading());
  CHECK_FALSE(texture.markLoading()); // loading -> loading
  CHECK(texture.state() == LoadState::kLoading);

  CHECK(texture.markReady());
  CHECK_FALSE(texture.markLoading());     // ready -> loading
  CHECK_FALSE(texture.markFailed("bad")); // ready -> failed
  CHECK(texture.state() == LoadState::kReady);
  CHECK(texture.error().empty());
}

TEST_CASE("resource: resource kinds and data defaults", "[asset][resource]") {
  Texture texture;
  Mesh mesh;
  Material material;

  CHECK(texture.kind() == ResourceKind::kTexture);
  CHECK(mesh.kind() == ResourceKind::kMesh);
  CHECK(material.kind() == ResourceKind::kMaterial);

  CHECK(mesh.vertices.empty());
  CHECK(mesh.indices.empty());
  CHECK_FALSE(material.baseColor.valid());
  CHECK(material.baseColorFactor == (yr::math::Vec3{1.0F, 1.0F, 1.0F}));
  CHECK(material.metallic == 0.0F);
  CHECK(material.roughness == 1.0F);
  CHECK(material.emissive == (yr::math::Vec3{0.0F, 0.0F, 0.0F}));

  const MeshVertex vertex;
  CHECK(vertex.position == (yr::math::Vec3{0.0F, 0.0F, 0.0F}));
  CHECK(vertex.normal == (yr::math::Vec3{0.0F, 0.0F, 1.0F}));
}

TEST_CASE("resource: Material holds Ref<Texture> for liveness", "[asset][resource][ref]") {
  const yr::obj::Ref<Texture> texture = yr::obj::make_ref<Texture>();
  REQUIRE(texture->refcount() == 1);

  {
    const yr::obj::Ref<Material> material = yr::obj::make_ref<Material>();
    REQUIRE(material->refcount() == 1);
    CHECK(material->kind() == ResourceKind::kMaterial);

    material->baseColor = texture;
    CHECK(texture->refcount() == 2);
    CHECK(material->baseColor.get() == texture.get());
    CHECK(material->baseColor->refcount() == 2);
  }

  CHECK(texture->refcount() == 1);
}

TEST_CASE("resource: make_ref works for Texture/Mesh/Material and Ref<Resource>", "[asset][resource][ref]") {
  {
    const yr::obj::Ref<Texture> texture = yr::obj::make_ref<Texture>();
    const yr::obj::Ref<Mesh> mesh = yr::obj::make_ref<Mesh>();
    const yr::obj::Ref<Material> material = yr::obj::make_ref<Material>();

    CHECK(texture->kind() == ResourceKind::kTexture);
    CHECK(mesh->kind() == ResourceKind::kMesh);
    CHECK(material->kind() == ResourceKind::kMaterial);
    CHECK(texture->refcount() == 1);
    CHECK(mesh->refcount() == 1);
    CHECK(material->refcount() == 1);
  }

  {
    const yr::obj::Ref<yr::asset::Resource> resource = yr::obj::make_ref<Texture>();
    CHECK(resource->kind() == ResourceKind::kTexture);
    CHECK(resource->refcount() == 1);
  }
}