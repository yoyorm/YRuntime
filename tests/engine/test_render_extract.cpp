#include <catch2/catch_test_macros.hpp>

#include <yr/engine/render_extract.h>
#include <yr/render/snapshot.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

#include <cstddef>
#include <memory>
#include <vector>

using yr::engine::RenderExtractSystem;
using yr::math::Mat4;
using yr::math::Vec3;
using yr::render::CameraPod;
using yr::render::RenderSnapshot;
using yr::scene::Node;
using yr::scene::SceneTree;

namespace {

  bool transformIsIdentity(const yr::render::TransformPod& transform) {
    for (std::size_t index = 0; index < 16; ++index) {
      const float expected = (index % 5 == 0) ? 1.0F : 0.0F;
      if (transform.m[index] != expected) {
        return false;
      }
    }
    return true;
  }

  bool worldMatches(const yr::render::TransformPod& world, const Mat4& global) {
    for (std::size_t index = 0; index < 16; ++index) {
      if (world.m[index] != global.m_[index]) {
        return false;
      }
    }
    return true;
  }

  bool cameraIsIdentity(const CameraPod& camera) {
    return transformIsIdentity(camera.view) && transformIsIdentity(camera.proj);
  }

  void collectPreorder(const Node& node, std::vector<const Node*>& out) {
    out.push_back(&node);
    for (std::size_t child = 0; child < node.childCount(); ++child) {
      collectPreorder(*node.childAt(child), out);
    }
  }

} // namespace

TEST_CASE("render_extract: preorder抽取每个树内节点且矩阵逐元素一致", "[engine][render][extract]") {
  SceneTree tree;

  auto root = std::make_unique<Node>();
  auto childA = std::make_unique<Node>();
  auto grandA1 = std::make_unique<Node>();
  auto grandA2 = std::make_unique<Node>();
  auto childB = std::make_unique<Node>();
  Node* const rawRoot = root.get();
  Node* const rawA = childA.get();
  Node* const rawA1 = grandA1.get();
  Node* const rawA2 = grandA2.get();
  Node* const rawB = childB.get();

  rawRoot->setPosition(Vec3{0.0F, 0.0F, 0.0F});
  rawA->setPosition(Vec3{1.0F, 2.0F, 3.0F});
  rawA1->setPosition(Vec3{0.0F, 0.0F, 5.0F});
  rawA2->setPosition(Vec3{0.5F, 0.0F, 0.0F});
  rawB->setPosition(Vec3{-1.0F, 0.0F, 0.0F});

  REQUIRE(rawA->addChild(std::move(grandA1)));
  REQUIRE(rawA->addChild(std::move(grandA2)));
  REQUIRE(rawRoot->addChild(std::move(childA)));
  REQUIRE(rawRoot->addChild(std::move(childB)));
  REQUIRE(tree.setRoot(std::move(root)));

  tree.refreshTransforms();

  std::vector<const Node*> expected;
  collectPreorder(*rawRoot, expected);
  REQUIRE(expected.size() == 5);
  CHECK(expected[0] == rawRoot);
  CHECK(expected[1] == rawA);
  CHECK(expected[2] == rawA1);
  CHECK(expected[3] == rawA2);
  CHECK(expected[4] == rawB);

  RenderSnapshot snapshot;
  snapshot.frame_index = 42;
  RenderExtractSystem extractor;
  extractor.extract(tree, snapshot);

  REQUIRE(snapshot.items.size() == expected.size());
  CHECK(snapshot.frame_index == 42); // extract 不改 frame_index
  CHECK(cameraIsIdentity(snapshot.camera));

  for (std::size_t index = 0; index < expected.size(); ++index) {
    CAPTURE(index);
    CHECK(snapshot.items[index].object.value == expected[index]->id().raw());
    CHECK(worldMatches(snapshot.items[index].world, expected[index]->globalTransform()));
    CHECK_FALSE(snapshot.items[index].mesh.isValid());
    CHECK_FALSE(snapshot.items[index].material.isValid());
  }

  // 世界矩阵确实携带层级平移（a1 的 global = root + a + a1 = (1,2,8)）。
  CHECK(snapshot.items[2].world.m[12] == 1.0F);
  CHECK(snapshot.items[2].world.m[13] == 2.0F);
  CHECK(snapshot.items[2].world.m[14] == 8.0F);
}

TEST_CASE("render_extract: 重复抽取清空旧条目且统计确定", "[engine][render][extract]") {
  SceneTree tree;
  auto root = std::make_unique<Node>();
  REQUIRE(root->addChild(std::make_unique<Node>()));
  REQUIRE(tree.setRoot(std::move(root)));
  tree.refreshTransforms();

  RenderSnapshot snapshot;
  RenderExtractSystem extractor;
  extractor.extract(tree, snapshot);
  const std::size_t firstCount = snapshot.items.size();
  REQUIRE(firstCount == 2);
  const std::uint64_t firstObject = snapshot.items[0].object.value;

  extractor.extract(tree, snapshot); // 第二次不得累加
  CHECK(snapshot.items.size() == firstCount);
  CHECK(snapshot.items[0].object.value == firstObject);
}

TEST_CASE("render_extract: 空树安全，items 空且相机默认 identity", "[engine][render][extract]") {
  SceneTree tree;
  RenderSnapshot snapshot;
  snapshot.frame_index = 9;

  RenderExtractSystem extractor;
  extractor.extract(tree, snapshot);

  CHECK(snapshot.items.empty());
  CHECK(snapshot.frame_index == 9);
  CHECK(cameraIsIdentity(snapshot.camera));
}