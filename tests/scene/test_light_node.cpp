#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <yr/math/vec3.h>
#include <yr/object/class_info.h>
#include <yr/scene/light_node.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_tree.h>

using yr::math::Vec3;
using yr::scene::LightNode;
using yr::scene::LightType;
using yr::scene::Node;
using yr::scene::SceneTree;

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
  CHECK(light.classInfo()->properties().empty());
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
