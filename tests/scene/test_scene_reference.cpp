#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <yr/core/string_id.h>
#include <yr/core/string_interner.h>
#include <yr/object/class_db.h>
#include <yr/scene/packed_scene.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_reader.h>
#include <yr/scene/scene_reference.h>
#include <yr/scene/scene_writer.h>

namespace {

  using yr::obj::ClassDB;
  using yr::scene::kInvalidParent;
  using yr::scene::PackedConnection;
  using yr::scene::PackedExtResource;
  using yr::scene::PackedNode;
  using yr::scene::PackedScene;
  using yr::scene::PackedSubResource;
  using yr::scene::PackedSubResourceProperty;
  using yr::scene::ReferenceKind;
  using yr::scene::SceneDiagnostic;
  using yr::scene::SceneLoadResult;
  using yr::scene::SceneReader;
  using yr::scene::SceneValue;
  using yr::scene::SceneWriter;
  using yr::scene::SceneWriteResult;

  [[nodiscard]] yr::core::StringId intern(const char* text) {
    return yr::core::globalStringInterner().intern(text);
  }

  [[nodiscard]] const ClassDB& classes() {
    yr::scene::register_scene_classes();
    return yr::obj::globalClassDB();
  }

  [[nodiscard]] SceneLoadResult load(std::string_view text) {
    const SceneReader reader(classes());
    return reader.read(text);
  }

  [[nodiscard]] bool hasErrorContaining(const SceneLoadResult& result, std::string_view needle) {
    for (const SceneDiagnostic& diagnostic : result.diagnostics) {
      if (diagnostic.severity == SceneDiagnostic::Severity::kError &&
          diagnostic.message.find(needle) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  constexpr std::string_view kReferenceScene =
      "[scene]\n"
      "\n"
      "[ext_resource type=\"Material\" id=\"mat_a\" path=\"res://a.yrres\"]\n"
      "\n"
      "[ext_resource type=\"Texture\" id=\"tex_b\" path=\"res://b.png\" uid=\"uid123\"]\n"
      "\n"
      "[sub_resource type=\"Material\" id=\"local_a\"]\n"
      "albedo = ExtResource(\"tex_b\")\n"
      "\n"
      "[sub_resource type=\"Material\" id=\"local_b\"]\n"
      "base = SubResource(\"local_a\")\n"
      "texture = ExtResource(\"mat_a\")\n"
      "\n"
      "[node name=\"Root\" type=\"Node\"]\n"
      "position = Vector3(0, 0, 0)\n"
      "\n"
      "[node name=\"Player\" type=\"Node\" parent=\".\"]\n"
      "position = Vector3(0, 1, 0)\n"
      "\n"
      "[node name=\"Hat\" type=\"Node\" parent=\"Player\"]\n"
      "position = Vector3(0, 0, 0)\n"
      "\n"
      "[connection from=\"Player\" to=\"Player/Hat\" signal=\"hit\" method=\"on_hit\"]\n";

} // namespace

TEST_CASE("scene reference: ext/sub/node/connection parse and resolve", "[scene][reference]") {
  const SceneLoadResult result = load(kReferenceScene);
  REQUIRE(result.ok());

  REQUIRE(result.scene.ext_resources.size() == 2);
  CHECK(result.scene.ext_resources[0].id == "mat_a");
  CHECK(result.scene.ext_resources[0].type.view() == "Material");
  CHECK(result.scene.ext_resources[0].path == "res://a.yrres");
  CHECK(result.scene.ext_resources[1].id == "tex_b");
  CHECK(result.scene.ext_resources[1].uid == "uid123");

  REQUIRE(result.scene.sub_resources.size() == 2);
  REQUIRE(result.scene.sub_resources[0].properties.size() == 1);
  const auto& albedo = result.scene.sub_resources[0].properties[0];
  CHECK(albedo.name.view() == "albedo");
  CHECK(albedo.value.isReference());
  CHECK(albedo.value.reference.kind == ReferenceKind::kExtResource);
  CHECK(albedo.value.reference.id == "tex_b");
  CHECK(albedo.value.reference.index == 1);

  REQUIRE(result.scene.sub_resources[1].properties.size() == 2);
  const auto& base = result.scene.sub_resources[1].properties[0];
  CHECK(base.name.view() == "base");
  CHECK(base.value.reference.kind == ReferenceKind::kSubResource);
  CHECK(base.value.reference.id == "local_a");
  CHECK(base.value.reference.index == 0);
  const auto& texture = result.scene.sub_resources[1].properties[1];
  CHECK(texture.name.view() == "texture");
  CHECK(texture.value.reference.kind == ReferenceKind::kExtResource);
  CHECK(texture.value.reference.index == 0);

  REQUIRE(result.scene.nodes.size() == 3);
  REQUIRE(result.scene.connections.size() == 1);
  const auto& connection = result.scene.connections[0];
  CHECK(connection.from == 1);
  CHECK(connection.to == 2);
  CHECK(connection.signal.view() == "hit");
  CHECK(connection.method.view() == "on_hit");
}

TEST_CASE("scene reference: save -> load -> save is byte-identical", "[scene][reference][roundtrip]") {
  const SceneLoadResult loaded = load(kReferenceScene);
  REQUIRE(loaded.ok());

  const SceneWriteResult first = SceneWriter::write(loaded.scene);
  REQUIRE(first.ok);
  CHECK(first.text == kReferenceScene);

  const SceneLoadResult reloaded = load(first.text);
  REQUIRE(reloaded.ok());
  const SceneWriteResult second = SceneWriter::write(reloaded.scene);
  REQUIRE(second.ok);
  CHECK(second.text == first.text);
}

TEST_CASE("scene reference: duplicate ext_resource id is an error", "[scene][reference]") {
  const SceneLoadResult result = load("[scene]\n"
                                      "[ext_resource type=\"Material\" id=\"dup\" path=\"res://a.yrres\"]\n"
                                      "[ext_resource type=\"Material\" id=\"dup\" path=\"res://b.yrres\"]\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = Vector3(0, 0, 0)\n");
  CHECK_FALSE(result.ok());
  CHECK(hasErrorContaining(result, "重复的 ext_resource id"));
}

TEST_CASE("scene reference: ext_resource path must start with res://", "[scene][reference]") {
  const SceneLoadResult result = load("[scene]\n"
                                      "[ext_resource type=\"Material\" id=\"a\" path=\"assets/a.yrres\"]\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = Vector3(0, 0, 0)\n");
  CHECK_FALSE(result.ok());
  CHECK(hasErrorContaining(result, "res://"));
}

TEST_CASE("scene reference: missing reference id is an error with source location", "[scene][reference]") {
  const SceneLoadResult result = load("[scene]\n"
                                      "[sub_resource type=\"Material\" id=\"a\"]\n"
                                      "tex = ExtResource(\"missing\")\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = Vector3(0, 0, 0)\n");
  CHECK_FALSE(result.ok());

  bool found = false;
  for (const SceneDiagnostic& diagnostic : result.diagnostics) {
    if (diagnostic.severity == SceneDiagnostic::Severity::kError &&
        diagnostic.message.find("missing") != std::string::npos) {
      found = true;
      CHECK(diagnostic.line > 0);
      CHECK(diagnostic.column > 0);
    }
  }
  CHECK(found);
}

TEST_CASE("scene reference: sub_resource cycle a -> b -> a is an error", "[scene][reference]") {
  const SceneLoadResult result = load("[scene]\n"
                                      "[sub_resource type=\"Material\" id=\"a\"]\n"
                                      "next = SubResource(\"b\")\n"
                                      "[sub_resource type=\"Material\" id=\"b\"]\n"
                                      "next = SubResource(\"a\")\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = Vector3(0, 0, 0)\n");
  CHECK_FALSE(result.ok());
  CHECK(hasErrorContaining(result, "a -> b -> a"));
}

TEST_CASE("scene reference: sub_resource self reference is an error", "[scene][reference]") {
  const SceneLoadResult result = load("[scene]\n"
                                      "[sub_resource type=\"Material\" id=\"a\"]\n"
                                      "next = SubResource(\"a\")\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = Vector3(0, 0, 0)\n");
  CHECK_FALSE(result.ok());
  CHECK(hasErrorContaining(result, "a -> a"));
}

TEST_CASE("scene reference: connection to missing node is an error", "[scene][reference]") {
  const SceneLoadResult result = load("[scene]\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = Vector3(0, 0, 0)\n"
                                      "[connection from=\".\" to=\"Ghost\" signal=\"hit\" method=\"on_hit\"]\n");
  CHECK_FALSE(result.ok());
  CHECK(hasErrorContaining(result, "Ghost"));
}

TEST_CASE("scene reference: connection before node is an error", "[scene][reference]") {
  const SceneLoadResult result = load("[scene]\n"
                                      "[connection from=\".\" to=\".\" signal=\"hit\" method=\"on_hit\"]\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = Vector3(0, 0, 0)\n");
  CHECK_FALSE(result.ok());
  CHECK(hasErrorContaining(result, "node 之后"));
}

TEST_CASE("scene reference: node properties cannot use reference syntax", "[scene][reference]") {
  const SceneLoadResult result = load("[scene]\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = ExtResource(\"x\")\n");
  CHECK_FALSE(result.ok());
  CHECK(hasErrorContaining(result, "node 属性暂不支持引用"));
}

TEST_CASE("scene reference: malformed reference syntax is an error", "[scene][reference]") {
  SECTION("missing closing parenthesis") {
    const SceneLoadResult result = load("[scene]\n"
                                        "[sub_resource type=\"Material\" id=\"a\"]\n"
                                        "tex = ExtResource(\"x\"\n"
                                        "[node name=\"Root\" type=\"Node\"]\n"
                                        "position = Vector3(0, 0, 0)\n");
    CHECK_FALSE(result.ok());
    CHECK(hasErrorContaining(result, "引用"));
  }
  SECTION("missing opening quote") {
    const SceneLoadResult result = load("[scene]\n"
                                        "[sub_resource type=\"Material\" id=\"a\"]\n"
                                        "tex = SubResource(x)\n"
                                        "[node name=\"Root\" type=\"Node\"]\n"
                                        "position = Vector3(0, 0, 0)\n");
    CHECK_FALSE(result.ok());
    CHECK(hasErrorContaining(result, "引用"));
  }
}

TEST_CASE("scene reference: duplicate connection is an error", "[scene][reference]") {
  const SceneLoadResult result = load("[scene]\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = Vector3(0, 0, 0)\n"
                                      "[connection from=\".\" to=\".\" signal=\"hit\" method=\"on_hit\"]\n"
                                      "[connection from=\".\" to=\".\" signal=\"hit\" method=\"on_hit\"]\n");
  CHECK_FALSE(result.ok());
  CHECK(hasErrorContaining(result, "重复的 connection"));
}
TEST_CASE("scene reference: writer validates IR", "[scene][reference][writer]") {
  const auto makeRootScene = []() {
    PackedScene scene;
    PackedNode root;
    root.type = intern("Node");
    root.name = "Root";
    root.parent_index = kInvalidParent;
    scene.nodes.push_back(std::move(root));
    return scene;
  };

  SECTION("duplicate ext id") {
    PackedScene scene = makeRootScene();
    scene.ext_resources.push_back(PackedExtResource{"dup", intern("Material"), "res://a.yrres", {}});
    scene.ext_resources.push_back(PackedExtResource{"dup", intern("Material"), "res://b.yrres", {}});
    const SceneWriteResult result = SceneWriter::write(scene);
    CHECK_FALSE(result.ok);
    CHECK(result.text.empty());
    CHECK(result.error.find("重复") != std::string::npos);
  }

  SECTION("reference id/index mismatch") {
    PackedScene scene = makeRootScene();
    scene.ext_resources.push_back(PackedExtResource{"a", intern("Material"), "res://a.yrres", {}});

    PackedSubResource resource;
    resource.id = "s";
    resource.type = intern("Material");
    SceneValue value;
    value.reference.kind = ReferenceKind::kExtResource;
    value.reference.id = "b";
    value.reference.index = 0;
    resource.properties.push_back(PackedSubResourceProperty{intern("tex"), value});
    scene.sub_resources.push_back(std::move(resource));

    const SceneWriteResult result = SceneWriter::write(scene);
    CHECK_FALSE(result.ok);
    CHECK(result.text.empty());
    CHECK(result.error.find("不一致") != std::string::npos);
  }

  SECTION("duplicate connection") {
    PackedScene scene = makeRootScene();
    scene.connections.push_back(PackedConnection{0, 0, intern("hit"), intern("on_hit")});
    scene.connections.push_back(PackedConnection{0, 0, intern("hit"), intern("on_hit")});
    const SceneWriteResult result = SceneWriter::write(scene);
    CHECK_FALSE(result.ok);
    CHECK(result.text.empty());
    CHECK(result.error.find("重复") != std::string::npos);
  }
}

TEST_CASE("scene reference: duplicate sub_resource id is an error with section location", "[scene][reference]") {
  const SceneLoadResult result = load("[scene]\n"
                                      "[sub_resource type=\"Material\" id=\"dup\"]\n"
                                      "[sub_resource type=\"Material\" id=\"dup\"]\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = Vector3(0, 0, 0)\n");
  CHECK_FALSE(result.ok());

  bool found = false;
  for (const SceneDiagnostic& diagnostic : result.diagnostics) {
    if (diagnostic.severity == SceneDiagnostic::Severity::kError &&
        diagnostic.message.find("重复的 sub_resource id") != std::string::npos) {
      found = true;
      CHECK(diagnostic.line == 3);
      CHECK(diagnostic.column > 1);
    }
  }
  CHECK(found);
}
