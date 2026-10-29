#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include <yr/core/string_interner.h>
#include <yr/math/vec3.h>
#include <yr/object/class_db.h>
#include <yr/object/class_info.h>
#include <yr/object/object.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/node.h>
#include <yr/scene/packed_scene.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_reader.h>
#include <yr/scene/scene_tree.h>
#include <yr/scene/scene_writer.h>

namespace {

  using yr::core::StringId;
  using yr::math::Vec3;
  using yr::obj::ClassDB;
  using yr::scene::Node;
  using yr::scene::PackedScene;
  using yr::scene::SceneDiagnostic;
  using yr::scene::SceneLoadResult;
  using yr::scene::SceneReader;
  using yr::scene::SceneTree;
  using yr::scene::SceneWriter;
  using yr::scene::SceneWriteResult;

  [[nodiscard]] StringId intern(const char* text) {
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

  [[nodiscard]] std::unique_ptr<Node> makeTree() {
    auto root = std::make_unique<Node>();
    auto player = std::make_unique<Node>();
    auto hat = std::make_unique<Node>();
    root->setName("Root");
    player->setName("Player");
    player->setPosition(Vec3{0.0F, 1.0F, 0.0F});
    hat->setName("Hat");
    hat->setPosition(Vec3{0.0F, 0.5F, 0.0F});
    REQUIRE(player->addChild(std::move(hat)));
    REQUIRE(root->addChild(std::move(player)));
    return root;
  }

  [[nodiscard]] std::unique_ptr<Node> namedNode(std::string_view name) {
    auto node = std::make_unique<Node>();
    node->setName(name);
    return node;
  }

  class NullClassNode final : public Node {
  public:
    [[nodiscard]] const yr::obj::ClassInfo* classInfo() const noexcept override { return nullptr; }
  };

  class NoGetterNode final : public Node {
  public:
    [[nodiscard]] const yr::obj::ClassInfo* classInfo() const noexcept override;
  };

  const yr::obj::ClassInfo& noGetterClass() {
    static const yr::obj::PropertyInfo props[] = {
        {.name = intern("broken"),
         .type = yr::obj::Variant::Type::kInt,
         .flags = static_cast<std::uint32_t>(yr::obj::kSerialized),
         .getter = nullptr,
         .setter = nullptr},
    };
    static const yr::obj::ClassInfo info(intern("NoGetterNode"), &yr::scene::nodeClassInfo(), props, nullptr);
    return info;
  }

  const yr::obj::ClassInfo* NoGetterNode::classInfo() const noexcept {
    return &noGetterClass();
  }

  class BadEncodeNode final : public Node {
  public:
    [[nodiscard]] const yr::obj::ClassInfo* classInfo() const noexcept override;
  };

  const yr::obj::ClassInfo& badEncodeClass() {
    static const yr::obj::PropertyInfo props[] = {
        {.name = intern("broken"),
         .type = yr::obj::Variant::Type::kInt,
         .flags = static_cast<std::uint32_t>(yr::obj::kSerialized),
         .getter = +[](const yr::obj::Object*) -> yr::obj::Variant { return yr::obj::Variant(yr::obj::Array{}); },
         .setter = nullptr},
    };
    static const yr::obj::ClassInfo info(intern("BadEncodeNode"), &yr::scene::nodeClassInfo(), props, nullptr);
    return info;
  }

  const yr::obj::ClassInfo* BadEncodeNode::classInfo() const noexcept {
    return &badEncodeClass();
  }

  constexpr std::string_view kManualScene = "[scene format=1]\n"
                                            "\n"
                                            "[node name=\"Root\" type=\"Node\"]\n"
                                            "\n"
                                            "[node name=\"Child\" type=\"Node\" parent=\".\"]\n"
                                            "position = Vector3(0, 1, 0)\n";

  constexpr std::string_view kUnknownPropertyScene = "[scene format=1]\n"
                                                     "[node name=\"Root\" type=\"Node\"]\n"
                                                     "mystery = 42\n"
                                                     "position = Vector3(1, 2, 3)\n";

  constexpr std::string_view kCanonicalTree = "[scene format=1]\n"
                                              "\n"
                                              "[node name=\"Root\" type=\"Node\"]\n"
                                              "position = Vector3(0, 0, 0)\n"
                                              "\n"
                                              "[node name=\"Player\" type=\"Node\" parent=\".\"]\n"
                                              "position = Vector3(0, 1, 0)\n"
                                              "\n"
                                              "[node name=\"Hat\" type=\"Node\" parent=\"Player\"]\n"
                                              "position = Vector3(0, 0.5, 0)\n";

} // namespace

TEST_CASE("scene format: hand-written 6-line .scn loads", "[scene][scene_format]") {
  const SceneLoadResult result = load(kManualScene);
  REQUIRE(result.ok());
  REQUIRE(result.scene.nodes.size() == 2);

  CHECK(result.scene.nodes[0].name == "Root");
  CHECK(result.scene.nodes[1].name == "Child");
  CHECK(result.scene.nodes[1].parent_path == ".");
  CHECK(result.scene.nodes[1].parent_index == 0);
  REQUIRE(result.scene.nodes[1].properties.size() == 1);
  CHECK(result.scene.nodes[1].properties[0].value.as<Vec3>() == Vec3{0.0F, 1.0F, 0.0F});
}

TEST_CASE("scene format: save -> load -> save is byte-identical", "[scene][scene_format][roundtrip]") {
  const std::unique_ptr<Node> tree = makeTree();
  const SceneWriteResult first = SceneWriter::write(*tree);
  REQUIRE(first.ok);
  CHECK_FALSE(first.text.empty());

  std::string error;
  std::ostringstream stream;
  REQUIRE(SceneWriter::write(*tree, stream, error));
  CHECK(error.empty());
  CHECK(stream.str() == first.text);

  const SceneLoadResult loaded = load(first.text);
  REQUIRE(loaded.ok());
  const std::unique_ptr<Node> instantiated = loaded.scene.instantiate(classes());
  REQUIRE(instantiated != nullptr);

  const SceneWriteResult second = SceneWriter::write(*instantiated);
  REQUIRE(second.ok);
  CHECK(first.text == second.text);
  CHECK(first.text == kCanonicalTree);
  CHECK(first.text.back() == '\n');
  CHECK(first.text.find("[scene format=1]\n") == 0);
  CHECK(first.text.find("parent=\".\"") != std::string::npos);
  CHECK(first.text.find("parent=\"Player\"") != std::string::npos);
}

TEST_CASE("scene writer: invalid node names fail without producing text", "[scene][scene_format][writer]") {
  for (const char* name : {"", "A/B", ".", "..", "A\"B", "A\nB"}) {
    CAPTURE(name);
    const std::unique_ptr<Node> root = namedNode(name);
    const SceneWriteResult result = SceneWriter::write(*root);
    CHECK_FALSE(result.ok);
    CHECK(result.text.empty());
    CHECK_FALSE(result.error.empty());
    CHECK(result.error.find("name") != std::string::npos);

    std::ostringstream stream;
    std::string streamError;
    CHECK_FALSE(SceneWriter::write(*root, stream, streamError));
    CHECK(stream.str().empty());
    CHECK(streamError == result.error);
  }
}

TEST_CASE("scene writer: duplicate sibling names are rejected", "[scene][scene_format][writer]") {
  const std::unique_ptr<Node> root = namedNode("Root");
  REQUIRE(root->addChild(namedNode("A")));
  REQUIRE(root->addChild(namedNode("A")));

  const SceneWriteResult result = SceneWriter::write(*root);
  CHECK_FALSE(result.ok);
  CHECK(result.text.empty());
  CHECK(result.error.find("重名") != std::string::npos);
}

TEST_CASE("scene writer: writing a subtree as root is rejected", "[scene][scene_format][writer]") {
  const std::unique_ptr<Node> root = namedNode("Root");
  REQUIRE(root->addChild(namedNode("Child")));
  const Node* const child = root->childAt(0);
  REQUIRE(child != nullptr);

  const SceneWriteResult result = SceneWriter::write(*child);
  CHECK_FALSE(result.ok);
  CHECK(result.text.empty());
  CHECK(result.error.find("parent") != std::string::npos);
}

TEST_CASE("scene writer: missing ClassInfo is rejected", "[scene][scene_format][writer]") {
  NullClassNode node;
  node.setName("Root");

  const SceneWriteResult result = SceneWriter::write(node);
  CHECK_FALSE(result.ok);
  CHECK(result.text.empty());
  CHECK(result.error.find("classInfo") != std::string::npos);
}

TEST_CASE("scene writer: serialized property without getter is rejected", "[scene][scene_format][writer]") {
  NoGetterNode node;
  node.setName("Root");

  const SceneWriteResult result = SceneWriter::write(node);
  CHECK_FALSE(result.ok);
  CHECK(result.text.empty());
  CHECK(result.error.find("getter") != std::string::npos);
}

TEST_CASE("scene writer: serialized property that fails encoding is rejected", "[scene][scene_format][writer]") {
  BadEncodeNode node;
  node.setName("Root");

  const SceneWriteResult result = SceneWriter::write(node);
  CHECK_FALSE(result.ok);
  CHECK(result.text.empty());
  CHECK(result.error.find("编码失败") != std::string::npos);
}

TEST_CASE("scene format: instantiate preserves names, positions and parents", "[scene][scene_format]") {
  const SceneLoadResult result = load(kManualScene);
  REQUIRE(result.ok());

  const std::unique_ptr<Node> root = result.scene.instantiate(classes());
  REQUIRE(root != nullptr);
  CHECK(root->name().view() == "Root");
  REQUIRE(root->childCount() == 1);

  const Node* const child = root->childAt(0);
  REQUIRE(child != nullptr);
  CHECK(child->name().view() == "Child");
  CHECK(child->parent() == root.get());
  CHECK(child->position() == Vec3{0.0F, 1.0F, 0.0F});
}

TEST_CASE("scene format: unknown property is a warning and parsing continues", "[scene][scene_format]") {
  const SceneLoadResult result = load(kUnknownPropertyScene);
  REQUIRE(result.ok());
  REQUIRE(result.scene.nodes.size() == 1);
  REQUIRE(result.diagnostics.size() == 1);
  CHECK(result.diagnostics[0].severity == SceneDiagnostic::Severity::kWarning);
  CHECK(result.diagnostics[0].line == 3);
  CHECK(result.diagnostics[0].column == 1);

  const std::unique_ptr<Node> root = result.scene.instantiate(classes());
  REQUIRE(root != nullptr);
  CHECK(root->position() == Vec3{1.0F, 2.0F, 3.0F});
}

TEST_CASE("scene format: unknown class is an error and does not crash", "[scene][scene_format]") {
  const SceneLoadResult result = load("[scene format=1]\n[node name=\"Root\" type=\"Gadget\"]\n");
  CHECK_FALSE(result.ok());
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics.back().line == 2);
  CHECK(result.diagnostics.back().message.find("Gadget") != std::string::npos);
}

TEST_CASE("scene format: '.' and '..' are reserved node names", "[scene][scene_format]") {
  for (const char* name : {".", ".."}) {
    CAPTURE(name);
    const std::string text = std::string("[scene format=1]\n[node name=\"") + name + "\" type=\"Node\"]\n";
    const SceneLoadResult result = load(text);
    CHECK_FALSE(result.ok());
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.back().line == 2);
    CHECK(result.diagnostics.back().column > 0);
    CHECK(result.diagnostics.back().message.find("name") != std::string::npos);
  }
}

TEST_CASE("scene format: parent must exist and appear before the child", "[scene][scene_format]") {
  SECTION("parent appears later") {
    const SceneLoadResult result = load("[scene format=1]\n"
                                        "[node name=\"Root\" type=\"Node\"]\n"
                                        "[node name=\"Child\" type=\"Node\" parent=\"Later\"]\n"
                                        "[node name=\"Later\" type=\"Node\" parent=\".\"]\n");
    CHECK_FALSE(result.ok());
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.back().line == 3);
  }

  SECTION("parent path does not exist") {
    const SceneLoadResult result = load("[scene format=1]\n"
                                        "[node name=\"Root\" type=\"Node\"]\n"
                                        "[node name=\"Child\" type=\"Node\" parent=\"Ghost\"]\n");
    CHECK_FALSE(result.ok());
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.back().line == 3);
  }
}

TEST_CASE("scene format: duplicate sibling names are an error", "[scene][scene_format]") {
  const SceneLoadResult result = load("[scene format=1]\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "[node name=\"A\" type=\"Node\" parent=\".\"]\n"
                                      "[node name=\"A\" type=\"Node\" parent=\".\"]\n");
  CHECK_FALSE(result.ok());
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics.back().line == 4);
  CHECK(result.diagnostics.back().message.find("重名") != std::string::npos);
}

TEST_CASE("scene format: duplicate property is an error", "[scene][scene_format]") {
  const SceneLoadResult result = load("[scene format=1]\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = Vector3(0, 0, 0)\n"
                                      "position = Vector3(1, 1, 1)\n");
  CHECK_FALSE(result.ok());
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics.back().line == 4);
  CHECK(result.diagnostics.back().message.find("重复属性") != std::string::npos);
}

TEST_CASE("scene format: missing, wrong or duplicate scene headers are errors", "[scene][scene_format]") {
  SECTION("no header") {
    const SceneLoadResult result = load("[node name=\"Root\" type=\"Node\"]\n");
    CHECK_FALSE(result.ok());
  }
  SECTION("wrong format") {
    const SceneLoadResult result = load("[scene format=2]\n");
    CHECK_FALSE(result.ok());
  }
  SECTION("missing format") {
    const SceneLoadResult result = load("[scene]\n");
    CHECK_FALSE(result.ok());
  }
  SECTION("duplicate header") {
    const SceneLoadResult result = load("[scene format=1]\n[scene format=1]\n");
    CHECK_FALSE(result.ok());
  }
  SECTION("no root node") {
    const SceneLoadResult result = load("[scene format=1]\n");
    CHECK_FALSE(result.ok());
  }
}

TEST_CASE("scene format: unknown header field and section are warnings", "[scene][scene_format]") {
  const SceneLoadResult result = load("[scene format=1 extra=yes]\n"
                                      "[bogus weird syntax]\n"
                                      "[node name=\"Root\" type=\"Node\"]\n"
                                      "position = Vector3(0, 0, 0)\n");
  REQUIRE(result.ok());
  REQUIRE(result.scene.nodes.size() == 1);
  REQUIRE(result.diagnostics.size() == 2);
  CHECK(result.diagnostics[0].severity == SceneDiagnostic::Severity::kWarning);
  CHECK(result.diagnostics[0].message.find("extra") != std::string::npos);
  CHECK(result.diagnostics[1].severity == SceneDiagnostic::Severity::kWarning);
  CHECK(result.diagnostics[1].message.find("bogus") != std::string::npos);
}

TEST_CASE("scene format: instantiated tree works with SceneTree::setRoot", "[scene][scene_format]") {
  const SceneLoadResult result = load(kManualScene);
  REQUIRE(result.ok());
  std::unique_ptr<Node> root = result.scene.instantiate(classes());
  REQUIRE(root != nullptr);

  SceneTree tree;
  REQUIRE(tree.setRoot(std::move(root)));
  REQUIRE(tree.root() != nullptr);
  CHECK(tree.root()->name().view() == "Root");
  REQUIRE(tree.root()->childCount() == 1);

  Node* const child = tree.root()->childAt(0);
  REQUIRE(child != nullptr);
  tree.refreshTransforms();
  CHECK(child->globalTransform()[13] == 1.0F);
}

TEST_CASE("scene classes: Node metadata is registered and exposes position", "[scene][scene_format]") {
  static_cast<void>(classes());

  const Node node;
  REQUIRE(node.classInfo() != nullptr);
  CHECK(node.classInfo()->name().view() == "Node");
  REQUIRE(node.classInfo()->parent() != nullptr);
  CHECK(node.classInfo()->parent()->name().view() == "Object");

  const yr::obj::PropertyInfo* const position = node.classInfo()->findProperty(intern("position"));
  REQUIRE(position != nullptr);
  CHECK(position->type == yr::obj::Variant::Type::kVec3);
  CHECK((position->flags & yr::obj::kSerialized) != 0);
}

TEST_CASE("scene format: BOM / CRLF input and istream overload load", "[scene][scene_format]") {
  std::istringstream stream(std::string("\xEF\xBB\xBF[scene format=1]\r\n[node name=\"Root\" type=\"Node\"]\r\n"));
  const SceneReader reader(classes());
  const SceneLoadResult result = reader.read(stream);
  REQUIRE(result.ok());
  REQUIRE(result.scene.nodes.size() == 1);
  CHECK(result.scene.nodes[0].name == "Root");
}