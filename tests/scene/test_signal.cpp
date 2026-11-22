#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <yr/core/string_interner.h>
#include <yr/object/class_db.h>
#include <yr/object/class_info.h>
#include <yr/object/object.h>
#include <yr/object/variant.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_reader.h>
#include <yr/scene/scene_tree.h>
#include <yr/scene/scene_writer.h>

#include "scene_test_support.h"

namespace {

  using yr::core::StringId;
  using yr::obj::ClassDB;
  using yr::obj::ClassInfo;
  using yr::obj::Object;
  using yr::obj::Variant;
  using yr::scene::SceneLoadResult;
  using yr::scene::SceneReader;
  using yr::scene::SceneWriter;
  using yr::scene::SceneWriteResult;

  [[nodiscard]] StringId intern(const char* text) {
    return yr::core::globalStringInterner().intern(text);
  }

  class ProbeNode final : public yr::scene::Node {
  public:
    [[nodiscard]] const ClassInfo* classInfo() const noexcept override;

    int count = 0;
    StringId lastMethod;
    Variant lastPayload;
  };

  [[nodiscard]] const ClassInfo& probeClass() {
    static const yr::obj::MethodInfo kMethods[] = {
        {.name = intern("add"),
         .fn =
             +[](Object* object, const Variant& payload) {
               auto* const node = static_cast<ProbeNode*>(object);
               ++node->count;
               node->lastMethod = intern("add");
               node->lastPayload = payload;
             }},
        {.name = intern("loop"),
         .fn =
             +[](Object* object, const Variant& payload) {
               auto* const node = static_cast<ProbeNode*>(object);
               ++node->count;
               node->lastMethod = intern("loop");
               node->lastPayload = payload;
               node->emitSignal(intern("loop"), payload);
             }},
    };
    static const ClassInfo info(
        intern("Probe"), &yr::scene::nodeClassInfo(), {},
        +[]() -> std::unique_ptr<Object> { return std::make_unique<ProbeNode>(); }, kMethods);
    return info;
  }

  const ClassInfo* ProbeNode::classInfo() const noexcept {
    return &probeClass();
  }

  void registerProbeClasses(ClassDB& classes) {
    classes.registerClass(yr::scene::nodeClassInfo());
    classes.registerClass(probeClass());
  }

} // namespace

TEST_CASE("signal: connection dispatches method and payload", "[scene][signal]") {
  ProbeNode source;
  ProbeNode target;
  source.connect(intern("hit"), target.id(), intern("add"));

  source.emitSignal(intern("hit"), Variant(static_cast<std::int64_t>(3)));

  CHECK(target.count == 1);
  CHECK(target.lastMethod.view() == "add");
  CHECK(target.lastPayload.as<std::int64_t>() == 3);
}

TEST_CASE("signal: dead target is skipped safely", "[scene][signal]") {
  ProbeNode source;
  {
    auto target = std::make_unique<ProbeNode>();
    source.connect(intern("hit"), target->id(), intern("add"));
  }

  source.emitSignal(intern("hit"), Variant(static_cast<std::int64_t>(1)));
  CHECK(source.count == 0);
}

TEST_CASE("signal: base queue_free method queues the target", "[scene][signal]") {
  ProbeNode source;
  auto target = std::make_unique<ProbeNode>();
  target->setName("Target");
  ProbeNode* const raw = target.get();

  yr::scene::SceneTree tree;
  REQUIRE(tree.setRoot(std::move(target)));

  source.connect(intern("gone"), raw->id(), intern("queue_free"));
  source.emitSignal(intern("gone"));

  CHECK(raw->isQueuedForDeletion());
}

TEST_CASE("signal: recursion depth is capped", "[scene][signal]") {
  yr_test::ScopedContinueAssertHandler guard;

  ProbeNode node;
  node.connect(intern("loop"), node.id(), intern("loop"));
  node.emitSignal(intern("loop"));

  CHECK(node.count == 8);
  CHECK(yr_test::g_assertCount >= 1);
}

TEST_CASE("signal: PackedScene::instantiate wires connections", "[scene][signal][instantiate]") {
  ClassDB classes;
  registerProbeClasses(classes);
  const SceneReader reader(classes);
  const SceneLoadResult loaded = reader.read("[scene]\n"
                                             "[node name=\"Root\" type=\"Probe\"]\n"
                                             "[node name=\"Child\" type=\"Probe\" parent=\".\"]\n"
                                             "[connection from=\".\" to=\"Child\" signal=\"hit\" method=\"add\"]\n");
  REQUIRE(loaded.ok());

  const std::unique_ptr<yr::scene::Node> root = loaded.scene.instantiate(classes);
  REQUIRE(root != nullptr);
  auto* const source = dynamic_cast<ProbeNode*>(root.get());
  REQUIRE(source != nullptr);
  REQUIRE(root->childCount() == 1);
  auto* const target = dynamic_cast<ProbeNode*>(root->childAt(0));
  REQUIRE(target != nullptr);

  source->emitSignal(intern("hit"), Variant(static_cast<std::int64_t>(7)));
  CHECK(target->count == 1);
  CHECK(target->lastPayload.as<std::int64_t>() == 7);
}

TEST_CASE("signal: writer serializes runtime connections", "[scene][signal][writer]") {
  ClassDB classes;
  registerProbeClasses(classes);

  auto root = std::make_unique<ProbeNode>();
  root->setName("Root");
  auto child = std::make_unique<ProbeNode>();
  child->setName("Child");
  ProbeNode* const rawChild = child.get();
  REQUIRE(root->addChild(std::move(child)));
  root->connect(intern("hit"), rawChild->id(), intern("add"));

  const SceneWriteResult written = SceneWriter::write(*root);
  REQUIRE(written.ok);
  CHECK(written.text.find("[connection") != std::string::npos);

  const SceneReader reader(classes);
  const SceneLoadResult loaded = reader.read(written.text);
  REQUIRE(loaded.ok());
  const std::unique_ptr<yr::scene::Node> reconstructed = loaded.scene.instantiate(classes);
  REQUIRE(reconstructed != nullptr);
  auto* const source = dynamic_cast<ProbeNode*>(reconstructed.get());
  REQUIRE(source != nullptr);
  REQUIRE(reconstructed->childCount() == 1);
  auto* const target = dynamic_cast<ProbeNode*>(reconstructed->childAt(0));
  REQUIRE(target != nullptr);

  source->emitSignal(intern("hit"), Variant(static_cast<std::int64_t>(9)));
  CHECK(target->count == 1);
  CHECK(target->lastPayload.as<std::int64_t>() == 9);

  const SceneWriteResult rewritten = SceneWriter::write(*reconstructed);
  REQUIRE(rewritten.ok);
  CHECK(rewritten.text == written.text);
}

TEST_CASE("signal: writer rejects a target outside the tree", "[scene][signal][writer]") {
  ProbeNode root;
  root.setName("Root");
  ProbeNode outside;
  outside.setName("Outside");
  root.connect(intern("hit"), outside.id(), intern("add"));

  const SceneWriteResult written = SceneWriter::write(root);
  CHECK_FALSE(written.ok);
  CHECK(written.text.empty());
}