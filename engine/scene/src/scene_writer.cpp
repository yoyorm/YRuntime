#include <yr/scene/scene_writer.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/node.h>
#include <yr/scene/packed_scene.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_format.h>
#include <yr/scene/scene_reference.h>
#include <yr/serialize/variant_codec.h>

namespace yr::scene {

  namespace {

    [[nodiscard]] std::string quoted(std::string_view text) {
      const yr::ser::VariantEncodeResult encoded = yr::ser::VariantCodec::encode(yr::obj::Variant(std::string(text)));
      return encoded.ok ? encoded.text : "\"\"";
    }

    [[nodiscard]] std::string nodeLabel(std::string_view path) {
      return path.empty() ? std::string("root") : "节点 '" + std::string(path) + "'";
    }

    void appendProperties(const yr::obj::ClassInfo& info, std::vector<const yr::obj::PropertyInfo*>& out) {
      if (info.parent() != nullptr) {
        appendProperties(*info.parent(), out);
      }
      for (const yr::obj::PropertyInfo& property : info.properties()) {
        out.push_back(&property);
      }
    }

    // 不含根名的父路径：根的直接子节点为 "."，更深层为 "A/B"。
    [[nodiscard]] std::string parentPath(const Node& node) {
      std::string path;
      for (const Node* ancestor = node.parent(); ancestor != nullptr && ancestor->parent() != nullptr;
           ancestor = ancestor->parent()) {
        if (!path.empty()) {
          path.insert(0, "/");
        }
        path.insert(0, ancestor->name().view());
      }
      return path.empty() ? std::string(".") : path;
    }

    // 从 Node 树构造 IR；校验 classInfo / name / serialized 属性。
    [[nodiscard]] bool appendNode(const Node& node, std::string_view path, std::size_t parentIndex, PackedScene& scene,
                                  std::string& error) {
      const std::string label = nodeLabel(path);

      const yr::obj::ClassInfo* const info = node.classInfo();
      if (info == nullptr) {
        error = label + ": classInfo() 为空";
        return false;
      }
      if (!info->isA(&nodeClassInfo())) {
        error = label + ": class '" + std::string(info->name().view()) + "' 不是 Node";
        return false;
      }
      if (!isValidNodeName(node.name().view())) {
        error = label + ": name 非法（不能为空、不能是 '.' 或 '..'、不能含 '/'、'\"' 或换行）";
        return false;
      }

      PackedNode packed;
      packed.type = info->name();
      packed.name = std::string(node.name().view());
      packed.parent_path = node.parent() == nullptr ? std::string{} : parentPath(node);
      packed.parent_index = parentIndex;

      std::vector<const yr::obj::PropertyInfo*> properties;
      appendProperties(*info, properties);
      std::unordered_set<std::string> propertyNames;
      for (const yr::obj::PropertyInfo* property : properties) {
        if ((property->flags & yr::obj::kSerialized) == 0) {
          continue;
        }
        if (property->getter == nullptr) {
          error = label + ": serialized 属性 '" + std::string(property->name.view()) + "' 没有 getter";
          return false;
        }
        const std::string propertyName(property->name.view());
        if (!propertyNames.insert(propertyName).second) {
          error = label + ": 重复的 serialized 属性 '" + propertyName + "'";
          return false;
        }
        const yr::obj::Variant value = property->getter(&node);
        const yr::ser::VariantEncodeResult encoded = yr::ser::VariantCodec::encode(value);
        if (!encoded.ok) {
          error = label + ": serialized 属性 '" + propertyName + "' 编码失败：" + encoded.error;
          return false;
        }
        packed.properties.push_back(SceneProperty{property->name, value});
      }

      const std::size_t index = scene.nodes.size();
      scene.nodes.push_back(std::move(packed));

      std::unordered_set<std::string> childNames;
      for (std::size_t childIndex = 0; childIndex < node.childCount(); ++childIndex) {
        const Node* const child = node.childAt(childIndex);
        if (child == nullptr) {
          error = label + ": childAt(" + std::to_string(childIndex) + ") 为空";
          return false;
        }
        const std::string childName(child->name().view());
        if (!childNames.insert(childName).second) {
          error = label + ": 同级重名 '" + childName + "'";
          return false;
        }
        const std::string childPath = path.empty() ? childName : std::string(path) + "/" + childName;
        if (!appendNode(*child, childPath, index, scene, error)) {
          return false;
        }
      }
      return true;
    }

    [[nodiscard]] bool buildScene(const Node& root, PackedScene& scene, std::string& error) {
      if (root.parent() != nullptr) {
        error = "root 不能有 parent（传入的是子树，不是整棵树）";
        return false;
      }
      return appendNode(root, root.name().view(), kInvalidParent, scene, error);
    }

    [[nodiscard]] bool validateReference(const PackedScene& scene, const SceneReference& reference,
                                         const std::string& ownerId, const std::string& propertyName,
                                         std::string& error) {
      if (reference.kind == ReferenceKind::kExtResource) {
        if (reference.index >= scene.ext_resources.size()) {
          error = "sub_resource '" + ownerId + "' 属性 '" + propertyName + "' 的 ext_resource 引用 index 越界";
          return false;
        }
        if (scene.ext_resources[reference.index].id != reference.id) {
          error =
              "sub_resource '" + ownerId + "' 属性 '" + propertyName + "' 的 reference.id 与 ext_resource 表项不一致";
          return false;
        }
        return true;
      }
      if (reference.kind == ReferenceKind::kSubResource) {
        if (reference.index >= scene.sub_resources.size()) {
          error = "sub_resource '" + ownerId + "' 属性 '" + propertyName + "' 的 sub_resource 引用 index 越界";
          return false;
        }
        if (scene.sub_resources[reference.index].id != reference.id) {
          error =
              "sub_resource '" + ownerId + "' 属性 '" + propertyName + "' 的 reference.id 与 sub_resource 表项不一致";
          return false;
        }
        return true;
      }
      error = "sub_resource '" + ownerId + "' 属性 '" + propertyName + "' 含未知引用类型";
      return false;
    }

    [[nodiscard]] bool validateScene(const PackedScene& scene, std::string& error) {
      std::unordered_set<std::string> extIds;
      for (const PackedExtResource& resource : scene.ext_resources) {
        if (!isValidReferenceId(resource.id)) {
          error = "ext_resource id 非法：'" + resource.id + "'";
          return false;
        }
        if (!extIds.insert(resource.id).second) {
          error = "重复的 ext_resource id：'" + resource.id + "'";
          return false;
        }
        if (resource.type.view().empty()) {
          error = "ext_resource '" + resource.id + "' 缺少 type";
          return false;
        }
        if (resource.path.empty() || !resource.path.starts_with("res://")) {
          error = "ext_resource '" + resource.id + "' path 必须以 res:// 开头";
          return false;
        }
      }

      std::unordered_set<std::string> subIds;
      for (const PackedSubResource& resource : scene.sub_resources) {
        if (!isValidReferenceId(resource.id)) {
          error = "sub_resource id 非法：'" + resource.id + "'";
          return false;
        }
        if (!subIds.insert(resource.id).second) {
          error = "重复的 sub_resource id：'" + resource.id + "'";
          return false;
        }
        if (resource.type.view().empty()) {
          error = "sub_resource '" + resource.id + "' 缺少 type";
          return false;
        }

        std::unordered_set<std::string> propertyNames;
        for (const PackedSubResourceProperty& property : resource.properties) {
          const std::string propertyName(property.name.view());
          if (!isValidPropertyName(propertyName)) {
            error = "sub_resource '" + resource.id + "' 属性名非法：'" + propertyName + "'";
            return false;
          }
          if (!propertyNames.insert(propertyName).second) {
            error = "sub_resource '" + resource.id + "' 重复属性：'" + propertyName + "'";
            return false;
          }
          if (property.value.isReference()) {
            if (!validateReference(scene, property.value.reference, resource.id, propertyName, error)) {
              return false;
            }
          } else {
            const yr::ser::VariantEncodeResult encoded = yr::ser::VariantCodec::encode(property.value.value);
            if (!encoded.ok) {
              error = "sub_resource '" + resource.id + "' 属性 '" + propertyName + "' 编码失败：" + encoded.error;
              return false;
            }
          }
        }
      }

      if (scene.nodes.empty()) {
        error = "没有根节点";
        return false;
      }
      if (scene.nodes.front().parent_index != kInvalidParent) {
        error = "根节点不能有 parent";
        return false;
      }

      std::unordered_map<std::size_t, std::unordered_set<std::string>> childNames;
      for (std::size_t index = 0; index < scene.nodes.size(); ++index) {
        const PackedNode& node = scene.nodes[index];
        const std::string label = "node[" + std::to_string(index) + "] '" + node.name + "'";

        if (!isValidNodeName(node.name)) {
          error = label + " name 非法（不能为空、不能是 '.' 或 '..'、不能含 '/'、'\"' 或换行）";
          return false;
        }
        if (node.type.view().empty()) {
          error = label + " 缺少 type";
          return false;
        }

        if (index == 0) {
          if (node.parent_index != kInvalidParent) {
            error = "根节点不能有 parent";
            return false;
          }
        } else {
          if (node.parent_index == kInvalidParent || node.parent_index >= index) {
            error = label + " parent_index 非法";
            return false;
          }
          if (!childNames[node.parent_index].insert(node.name).second) {
            error = label + " 与兄弟重名";
            return false;
          }
        }

        std::unordered_set<std::string> propertyNames;
        for (const SceneProperty& property : node.properties) {
          const std::string propertyName(property.name.view());
          if (!isValidPropertyName(propertyName)) {
            error = label + " 属性名非法：'" + propertyName + "'";
            return false;
          }
          if (!propertyNames.insert(propertyName).second) {
            error = label + " 重复属性：'" + propertyName + "'";
            return false;
          }
          const yr::ser::VariantEncodeResult encoded = yr::ser::VariantCodec::encode(property.value);
          if (!encoded.ok) {
            error = label + " 属性 '" + propertyName + "' 编码失败：" + encoded.error;
            return false;
          }
        }
      }

      for (std::size_t index = 0; index < scene.connections.size(); ++index) {
        const PackedConnection& connection = scene.connections[index];
        if (connection.from >= scene.nodes.size() || connection.to >= scene.nodes.size()) {
          error = "connection 的 from/to 节点索引越界";
          return false;
        }
        if (connection.signal.view().empty() || connection.method.view().empty()) {
          error = "connection 的 signal/method 不能为空";
          return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
          const PackedConnection& other = scene.connections[previous];
          if (other.from == connection.from && other.to == connection.to && other.signal == connection.signal &&
              other.method == connection.method) {
            error = "重复的 connection";
            return false;
          }
        }
      }
      return true;
    }

    [[nodiscard]] std::string nodePathAt(const std::vector<std::string>& fullPaths, std::size_t index) {
      if (index == 0) {
        return ".";
      }
      return fullPaths[index].substr(fullPaths[0].size() + 1);
    }

  } // namespace

  SceneWriteResult SceneWriter::write(const Node& root) {
    PackedScene scene;
    std::string error;
    if (!buildScene(root, scene, error)) {
      return SceneWriteResult{false, {}, std::move(error)};
    }
    return write(scene);
  }

  SceneWriteResult SceneWriter::write(const PackedScene& scene) {
    std::string error;
    if (!validateScene(scene, error)) {
      return SceneWriteResult{false, {}, std::move(error)};
    }

    std::ostringstream out;
    out << kSceneHeader << '\n';

    for (const PackedExtResource& resource : scene.ext_resources) {
      out << "\n[ext_resource type=" << quoted(resource.type.view()) << " id=" << quoted(resource.id)
          << " path=" << quoted(resource.path);
      if (!resource.uid.empty()) {
        out << " uid=" << quoted(resource.uid);
      }
      out << "]\n";
    }

    for (const PackedSubResource& resource : scene.sub_resources) {
      out << "\n[sub_resource type=" << quoted(resource.type.view()) << " id=" << quoted(resource.id) << "]\n";

      std::vector<const PackedSubResourceProperty*> properties;
      properties.reserve(resource.properties.size());
      for (const PackedSubResourceProperty& property : resource.properties) {
        properties.push_back(&property);
      }
      std::sort(properties.begin(), properties.end(),
                [](const PackedSubResourceProperty* lhs, const PackedSubResourceProperty* rhs) {
                  return lhs->name.view() < rhs->name.view();
                });

      for (const PackedSubResourceProperty* property : properties) {
        out << property->name.view() << " = ";
        if (property->value.isReference()) {
          const SceneReference& reference = property->value.reference;
          if (reference.kind == ReferenceKind::kExtResource) {
            out << "ExtResource(" << quoted(scene.ext_resources[reference.index].id) << ")";
          } else {
            out << "SubResource(" << quoted(scene.sub_resources[reference.index].id) << ")";
          }
        } else {
          const yr::ser::VariantEncodeResult encoded = yr::ser::VariantCodec::encode(property->value.value);
          if (!encoded.ok) {
            return SceneWriteResult{false, {}, "sub_resource 属性编码失败：" + encoded.error};
          }
          out << encoded.text;
        }
        out << '\n';
      }
    }

    std::vector<std::string> fullPaths(scene.nodes.size());
    fullPaths[0] = scene.nodes[0].name;
    for (std::size_t index = 1; index < scene.nodes.size(); ++index) {
      fullPaths[index] = fullPaths[scene.nodes[index].parent_index] + "/" + scene.nodes[index].name;
    }

    for (std::size_t index = 0; index < scene.nodes.size(); ++index) {
      const PackedNode& node = scene.nodes[index];
      out << "\n[node name=" << quoted(node.name) << " type=" << quoted(node.type.view());
      if (index != 0) {
        out << " parent=" << quoted(nodePathAt(fullPaths, node.parent_index));
      }
      out << "]\n";

      for (const SceneProperty& property : node.properties) {
        const yr::ser::VariantEncodeResult encoded = yr::ser::VariantCodec::encode(property.value);
        if (!encoded.ok) {
          return SceneWriteResult{false, {}, "node 属性编码失败：" + encoded.error};
        }
        out << property.name.view() << " = " << encoded.text << '\n';
      }
    }

    for (const PackedConnection& connection : scene.connections) {
      out << "\n[connection from=" << quoted(nodePathAt(fullPaths, connection.from))
          << " to=" << quoted(nodePathAt(fullPaths, connection.to)) << " signal=" << quoted(connection.signal.view())
          << " method=" << quoted(connection.method.view()) << "]\n";
    }

    SceneWriteResult result;
    result.ok = true;
    result.text = out.str();
    return result;
  }

  bool SceneWriter::write(const Node& root, std::ostream& out, std::string& error) {
    SceneWriteResult result = write(root);
    if (!result.ok) {
      error = std::move(result.error);
      return false;
    }
    out << result.text;
    if (!out) {
      error = "写入 ostream 失败";
      return false;
    }
    return true;
  }

} // namespace yr::scene
