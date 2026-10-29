#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <yr/core/string_id.h>
#include <yr/object/variant.h>
#include <yr/scene/scene_format.h>
#include <yr/scene/scene_reference.h>

namespace yr::obj {
  class ClassDB;
}

namespace yr::scene {

  class Node;

  struct SceneProperty {
    core::StringId name;
    obj::Variant value;
  };

  // 场景的纯数据表示：解析阶段不创建 Node。
  struct PackedNode {
    core::StringId type;
    std::string name;
    // 根节点为空；根的直接子节点为 "."；其余为相对根、不含根名的路径（如 "Player/Hat"）。
    std::string parent_path;
    std::size_t parent_index = kInvalidParent;
    std::vector<SceneProperty> properties;
  };

  struct PackedExtResource {
    std::string id;
    core::StringId type;
    std::string path; // 必须以 "res://" 开头
    std::string uid;  // 可选
  };

  struct PackedSubResourceProperty {
    core::StringId name;
    SceneValue value;
  };

  struct PackedSubResource {
    std::string id;
    core::StringId type;
    std::vector<PackedSubResourceProperty> properties; // 源文件顺序
  };

  struct PackedConnection {
    std::size_t from = kInvalidIndex;
    std::size_t to = kInvalidIndex;
    core::StringId signal;
    core::StringId method;
  };

  class PackedScene {
  public:
    // 两阶段实例化：先按 ClassDB 创建全部节点，再按 parent_index 挂接。
    // 任一步失败返回 nullptr,不留下半成品树。
    [[nodiscard]] std::unique_ptr<Node> instantiate(const obj::ClassDB& classes) const;

    // 前序顺序；nodes[0] 是根。
    std::vector<PackedNode> nodes;
    std::vector<PackedExtResource> ext_resources;
    std::vector<PackedSubResource> sub_resources;
    std::vector<PackedConnection> connections;
  };

} // namespace yr::scene
