#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include <yr/asset/resource_types.h>
#include <yr/core/string_interner.h>
#include <yr/math/quat.h>
#include <yr/math/vec3.h>
#include <yr/object/class_info.h>
#include <yr/object/object_db.h>
#include <yr/scene/light_node.h>
#include <yr/scene/mesh_instance_node.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_tree.h>
#include <yr/scene/scene_writer.h>

using yr::asset::Material;
using yr::asset::Mesh;
using yr::math::Vec3;
using yr::obj::Variant;
using yr::scene::LightNode;
using yr::scene::LightType;
using yr::scene::MeshInstanceNode;
using yr::scene::Node;
using yr::scene::SceneTree;

namespace {

  [[nodiscard]] yr::core::StringId intern(const char* text) {
    return yr::core::globalStringInterner().intern(text);
  }

} // namespace

TEST_CASE("LightNode defaults are directional white light", "[scene][light]") {
  LightNode light;
  CHECK(light.type() == LightType::kDirectional);
  CHECK(light.color() == Vec3{1.0F, 1.0F, 1.0F});
  CHECK(light.intensity() == 1.0F);
  CHECK(light.range() == 10.0F);
  CHECK(light.direction() == Vec3{0.0F, 0.0F, -1.0F});
}

TEST_CASE("LightNode setters store type, color, intensity and range", "[scene][light]") {
  LightNode light;

  light.setType(LightType::kPoint);
  CHECK(light.type() == LightType::kPoint);

  light.setColor(Vec3{0.25F, 0.5F, 0.75F});
  CHECK(light.color() == Vec3{0.25F, 0.5F, 0.75F});

  light.setIntensity(3.5F);
  CHECK(light.intensity() == 3.5F);

  light.setRange(42.0F);
  CHECK(light.range() == 42.0F);
}

TEST_CASE("LightNode clamps intensity and range at zero", "[scene][light]") {
  LightNode light;
  light.setIntensity(-2.0F);
  CHECK(light.intensity() == 0.0F);
  light.setRange(-7.0F);
  CHECK(light.range() == 0.0F);

  light.setIntensity(0.0F);
  CHECK(light.intensity() == 0.0F);
  light.setRange(0.0F);
  CHECK(light.range() == 0.0F);
}

TEST_CASE("LightNode normalizes direction", "[scene][light]") {
  LightNode light;
  light.setDirection(Vec3{0.0F, 0.0F, -5.0F});
  CHECK(light.direction() == Vec3{0.0F, 0.0F, -1.0F});

  light.setDirection(Vec3{3.0F, 0.0F, 4.0F});
  CHECK(light.direction().x == Catch::Approx(0.6F));
  CHECK(light.direction().y == Catch::Approx(0.0F));
  CHECK(light.direction().z == Catch::Approx(0.8F));
  CHECK(light.direction().length() == Catch::Approx(1.0F));
}

TEST_CASE("LightNode falls back to -Z for a degenerate direction", "[scene][light]") {
  LightNode light;
  light.setDirection(Vec3{0.0F, 0.0F, 0.0F});
  CHECK(light.direction() == Vec3{0.0F, 0.0F, -1.0F});

  light.setDirection(Vec3{1.0F, 0.0F, 0.0F});
  light.setDirection(Vec3{1e-9F, 0.0F, 0.0F});
  CHECK(light.direction() == Vec3{0.0F, 0.0F, -1.0F});
}

TEST_CASE("LightNode metadata derives from Node", "[scene][light]") {
  LightNode light;
  REQUIRE(light.classInfo() != nullptr);
  CHECK(light.classInfo()->name().view() == "LightNode");
  CHECK(light.classInfo()->parent() == &yr::scene::nodeClassInfo());
  CHECK(light.classInfo()->properties().size() == 5);
}

TEST_CASE("LightNode participates in the scene hierarchy", "[scene][light]") {
  SceneTree tree;
  auto root = std::make_unique<Node>();
  auto light = std::make_unique<LightNode>();
  LightNode* const rawLight = light.get();
  Node* const rawRoot = root.get();

  light->setPosition(Vec3{1.0F, 2.0F, 3.0F});
  REQUIRE(rawLight->addChild(std::make_unique<LightNode>()));
  REQUIRE(root->addChild(std::move(light)));
  REQUIRE(tree.setRoot(std::move(root)));
  tree.refreshTransforms();

  CHECK(rawLight->parent() == rawRoot);
  CHECK(rawLight->tree() == &tree);
  CHECK(rawLight->childCount() == 1);
  CHECK(rawLight->globalTransform().transformPoint(Vec3{}) == Vec3{1.0F, 2.0F, 3.0F});
}

TEST_CASE("MeshInstanceNode metadata derives from Node", "[scene][mesh_instance]") {
  MeshInstanceNode node;
  REQUIRE(node.classInfo() != nullptr);
  CHECK(node.classInfo()->name().view() == "MeshInstanceNode");
  CHECK(node.classInfo()->parent() == &yr::scene::nodeClassInfo());
  REQUIRE(node.classInfo()->properties().size() == 2);
  CHECK(node.classInfo()->properties()[0].name.view() == "mesh");
  CHECK(node.classInfo()->properties()[0].class_hint.view() == "Mesh");
  CHECK(node.classInfo()->properties()[1].name.view() == "material");
  CHECK(node.classInfo()->properties()[1].class_hint.view() == "Material");
}

TEST_CASE("MeshInstanceNode references retain and release resources", "[scene][mesh_instance][ref]") {
  auto mesh = yr::obj::make_ref<Mesh>();
  auto material = yr::obj::make_ref<Material>();
  REQUIRE(mesh->refcount() == 1);
  REQUIRE(material->refcount() == 1);

  {
    MeshInstanceNode node;
    node.setMesh(mesh);
    node.setMaterial(material);
    CHECK(mesh->refcount() == 2);
    CHECK(material->refcount() == 2);

    node.setMesh({});
    node.setMaterial({});
    CHECK(mesh->refcount() == 1);
    CHECK(material->refcount() == 1);
  }

  const yr::core::ObjectID meshId = mesh->id();
  mesh.reset();
  CHECK(yr::obj::globalObjectDB().get(meshId) == nullptr);

  const yr::core::ObjectID materialId = material->id();
  material.reset();
  CHECK(yr::obj::globalObjectDB().get(materialId) == nullptr);
}

TEST_CASE("MeshInstanceNode destruction releases its final resource Ref", "[scene][mesh_instance][ref]") {
  yr::core::ObjectID meshId;
  yr::core::ObjectID materialId;
  {
    MeshInstanceNode node;
    auto mesh = yr::obj::make_ref<Mesh>();
    auto material = yr::obj::make_ref<Material>();
    meshId = mesh->id();
    materialId = material->id();

    node.setMesh(std::move(mesh));
    node.setMaterial(std::move(material));
    CHECK_FALSE(mesh.valid());
    CHECK_FALSE(material.valid());
    REQUIRE(node.mesh().valid());
    REQUIRE(node.material().valid());
    CHECK(node.mesh()->refcount() == 1);
    CHECK(node.material()->refcount() == 1);
  }
  CHECK(yr::obj::globalObjectDB().get(meshId) == nullptr);
  CHECK(yr::obj::globalObjectDB().get(materialId) == nullptr);
}

TEST_CASE("MeshInstanceNode inherits Node TRS and writer rejects retained assets", "[scene][mesh_instance]") {
  MeshInstanceNode node;
  node.setName("Root");
  node.setPosition(yr::math::Vec3{1.0F, 2.0F, 3.0F});
  node.setRotation(yr::math::Quat::fromAxisAngle(yr::math::Vec3{0.0F, 1.0F, 0.0F}, 1.0F));
  node.setScale(yr::math::Vec3{2.0F, 2.0F, 2.0F});
  CHECK(node.position() == yr::math::Vec3{1.0F, 2.0F, 3.0F});
  CHECK(node.scale() == yr::math::Vec3{2.0F, 2.0F, 2.0F});

  node.setRotation(yr::math::Quat::identity());
  node.setScale(yr::math::Vec3{1.0F, 1.0F, 1.0F});
  const yr::scene::SceneWriteResult emptyResult = yr::scene::SceneWriter::write(node);
  CHECK(emptyResult.ok);
  CHECK_FALSE(emptyResult.text.empty());

  node.setMesh(yr::obj::make_ref<Mesh>());
  const yr::scene::SceneWriteResult result = yr::scene::SceneWriter::write(node);
  CHECK_FALSE(result.ok);
  CHECK(result.text.empty());
  CHECK(result.error.find("MeshInstanceNode") != std::string::npos);
}

TEST_CASE("LightNode exposes serializable properties", "[scene][light][property]") {
  LightNode light;
  const yr::obj::ClassInfo& info = yr::scene::lightNodeClassInfo();

  CHECK(info.set_property(&light, intern("type"), Variant(std::string("point"))));
  CHECK(info.set_property(&light, intern("color"), Variant(Vec3{0.2F, 0.4F, 0.6F})));
  CHECK(info.set_property(&light, intern("intensity"), Variant(3.5)));
  CHECK(info.set_property(&light, intern("range"), Variant(25.0)));
  CHECK(info.set_property(&light, intern("direction"), Variant(Vec3{0.0F, -2.0F, 0.0F})));

  CHECK(light.type() == LightType::kPoint);
  CHECK(light.color() == Vec3{0.2F, 0.4F, 0.6F});
  CHECK(light.intensity() == 3.5F);
  CHECK(light.range() == 25.0F);
  CHECK(light.direction() == Vec3{0.0F, -1.0F, 0.0F});

  CHECK_FALSE(info.set_property(&light, intern("type"), Variant(std::string("spot"))));
  CHECK(light.type() == LightType::kPoint);
}

TEST_CASE("LightNode type property rejects wrong Variant type", "[scene][light][property]") {
  LightNode light;
  const yr::obj::ClassInfo& info = yr::scene::lightNodeClassInfo();
  CHECK_FALSE(info.set_property(&light, intern("type"), Variant(1.0)));
  CHECK_FALSE(info.set_property(&light, intern("intensity"), Variant(true)));
}
