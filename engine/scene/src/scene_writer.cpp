#include <yr/scene/scene_writer.h>

#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_format.h>
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

    [[nodiscard]] bool writeNode(const Node& node, std::string_view path, std::ostream& out, std::string& error) {
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

      out << "\n[node name=" << quoted(node.name().view()) << " type=" << quoted(info->name().view());
      if (node.parent() != nullptr) {
        out << " parent=" << quoted(parentPath(node));
      }
      out << "]\n";

      std::vector<const yr::obj::PropertyInfo*> properties;
      appendProperties(*info, properties);
      for (const yr::obj::PropertyInfo* property : properties) {
        if ((property->flags & yr::obj::kSerialized) == 0) {
          continue;
        }
        if (property->getter == nullptr) {
          error = label + ": serialized 属性 '" + std::string(property->name.view()) + "' 没有 getter";
          return false;
        }
        const yr::ser::VariantEncodeResult encoded = yr::ser::VariantCodec::encode(property->getter(&node));
        if (!encoded.ok) {
          error = label + ": serialized 属性 '" + std::string(property->name.view()) + "' 编码失败：" + encoded.error;
          return false;
        }
        out << property->name.view() << " = " << encoded.text << '\n';
      }

      std::unordered_set<std::string> childNames;
      for (std::size_t index = 0; index < node.childCount(); ++index) {
        const Node* const child = node.childAt(index);
        if (child == nullptr) {
          error = label + ": childAt(" + std::to_string(index) + ") 为空";
          return false;
        }
        const std::string childName(child->name().view());
        if (!childNames.insert(childName).second) {
          error = label + ": 同级重名 '" + childName + "'";
          return false;
        }
        const std::string childPath = path.empty() ? childName : std::string(path) + "/" + childName;
        if (!writeNode(*child, childPath, out, error)) {
          return false;
        }
      }
      return true;
    }

  } // namespace

  SceneWriteResult SceneWriter::write(const Node& root) {
    if (root.parent() != nullptr) {
      return SceneWriteResult{false, {}, "root 不能有 parent（传入的是子树，不是整棵树）"};
    }

    std::ostringstream out;
    out << kSceneHeader << '\n';

    std::string error;
    if (!writeNode(root, root.name().view(), out, error)) {
      return SceneWriteResult{false, {}, std::move(error)};
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