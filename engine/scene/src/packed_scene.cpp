#include "yr/object/object.h"
#include <yr/scene/packed_scene.h>

#include <memory>
#include <utility>
#include <vector>

#include <yr/object/class_db.h>
#include <yr/object/class_info.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_classes.h>

namespace yr::scene {

  // 实例化：ClassDB 创建全部 Node；setName/set_property；
  // 按 parent_index addChild；失败返回 nullptr。
  std::unique_ptr<Node> PackedScene::instantiate(const obj::ClassDB& classes) const {
    // 创建每个Node；根必须没有 parent。
    if (nodes.empty() || nodes.front().parent_index != kInvalidParent)
      return nullptr;

    std::vector<std::unique_ptr<Node>> owners;
    std::vector<Node*> raw;
    owners.reserve(nodes.size());
    raw.reserve(nodes.size());

    for (const PackedNode& packed : nodes) {
      std::unique_ptr<obj::Object> owned(classes.instantiate(packed.type));
      if (!owned)
        return nullptr; // 未注册
      const obj::ClassInfo* const info = owned->classInfo();
      if (info == nullptr || !info->isA(&nodeClassInfo()))
        return nullptr; // 不是Node或者派生类

      std::unique_ptr<Node> node(static_cast<Node*>(owned.release()));
      node->setName(packed.name);

      for (const SceneProperty& property : packed.properties) {
        if (!info->set_property(node.get(), property.name, property.value)) {
          return nullptr;
        }
      }
      raw.push_back(node.get());
      owners.push_back(std::move(node));
    }

    // 按parent_index 创建树
    for (std::size_t i = 1; i < nodes.size(); ++i) {
      const std::size_t parentIndex = nodes[i].parent_index;
      if (parentIndex == kInvalidParent || parentIndex >= i) {
        return nullptr;
      }

      Node* const parent = raw[parentIndex];
      if (parent == nullptr || !parent->addChild(std::move(owners[i]))) {
        return nullptr;
      }
    }

    return std::move(owners[0]); // root
  }

} // namespace yr::scene