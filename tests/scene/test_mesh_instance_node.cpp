#include <catch2/catch_test_macros.hpp>

#include <yr/asset/resource_types.h>
#include <yr/math/quat.h>
#include <yr/object/class_info.h>
#include <yr/object/object_db.h>
#include <yr/scene/mesh_instance_node.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_writer.h>

#include <memory>
#include <string>

using yr::asset::Material;
using yr::asset::Mesh;
using yr::scene::MeshInstanceNode;

TEST_CASE("MeshInstanceNode metadata derives from Node", "[scene][mesh_instance]") {
  MeshInstanceNode node;
  REQUIRE(node.classInfo() != nullptr);
  CHECK(node.classInfo()->name().view() == "MeshInstanceNode");
  CHECK(node.classInfo()->parent() == &yr::scene::nodeClassInfo());
  CHECK(node.classInfo()->properties().empty());
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
