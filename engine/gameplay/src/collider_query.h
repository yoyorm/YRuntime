#pragma once

#include <vector>

#include <yr/gameplay/box_collider.h>

namespace yr::gameplay::detail {

  // 扫整棵树收集 BoxCollider。第一版没有中央 CollisionWorld，先按需遍历。
  inline void collectColliders(const scene::Node& node, std::vector<const BoxCollider*>& out) {
    if (const auto* collider = dynamic_cast<const BoxCollider*>(&node); collider != nullptr) {
      out.push_back(collider);
    }
    for (std::size_t index = 0; index < node.childCount(); ++index) {
      if (const scene::Node* child = node.childAt(index); child != nullptr) {
        collectColliders(*child, out);
      }
    }
  }

} // namespace yr::gameplay::detail