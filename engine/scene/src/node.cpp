#include <yr/scene/node.h>

#include <algorithm>

#include <yr/core/assert.h>

namespace yr::scene {

  bool Node::addChild(std::unique_ptr<Node>&& child) {
    Node* const raw = child.get();

    if (raw == nullptr) {
      YR_ASSERT_MSG(false, "Node::addChild: child is null");
      return false;
    }
    if (raw == this) {
      YR_ASSERT_MSG(false, "Node::addChild: cannot add self as a child");
      return false;
    }
    if (raw->parent_ != nullptr) {
      YR_ASSERT_MSG(false, "Node::addChild: child already has a parent");
      return false;
    }
    // 沿 parent 链向上检查，防止形成环。
    for (Node* ancestor = this; ancestor != nullptr; ancestor = ancestor->parent_) {
      if (ancestor == raw) {
        YR_ASSERT_MSG(false, "Node::addChild: would create a cycle");
        return false;
      }
    }

    raw->parent_ = this;
    children_.push_back(std::move(child));

    // 父已在树内：只让新加入的子树 enter（前序），已有节点不重复 enter。
    // parent/children 关系此时已建立，回调能看到完整结构。
    if (tree_ != nullptr) {
      raw->setTree(tree_);
    }
    return true;
  }

  Node* Node::childAt(std::size_t index) const noexcept {
    if (index >= children_.size()) {
      YR_ASSERT_MSG(false, "Node::childAt: index out of bounds");
      return nullptr;
    }
    return children_[index].get();
  }

  std::unique_ptr<Node> Node::removeChild(Node* child) {
    if (child == nullptr) {
      YR_ASSERT_MSG(false, "Node::removeChild: child is null");
      return nullptr;
    }

    const auto it = std::find_if(children_.begin(), children_.end(),
                                 [child](const std::unique_ptr<Node>& entry) { return entry.get() == child; });
    if (it == children_.end()) {
      YR_ASSERT_MSG(false, "Node::removeChild: child does not belong to this parent");
      return nullptr;
    }

    // 在 move/erase/清 parent_ 之前派发 exit：回调期间 child 仍留在 children_
    // 且 parent_ 仍有效，可看到完整的父子关系。
    Node* const raw = it->get();
    if (raw->tree_ != nullptr) {
      raw->setTree(nullptr);
    }

    // 先 move 出拥有关系，再 erase, 避免在 erase 后访问已释放的 unique_ptr。
    std::unique_ptr<Node> owned = std::move(*it);
    children_.erase(it);
    owned->parent_ = nullptr;
    return owned;
  }

  std::unique_ptr<Node> Node::detach() {
    Node* const oldParent = parent_;
    if (oldParent == nullptr) {
      return nullptr;
    }
    return oldParent->removeChild(this);
  }

  // 树内状态的唯一入口。语义：
  //   - tree == tree_：安全 no-op（重复设置不重复派发）；
  //   - 先离开旧树：完整 propagateExitTree()（后序、子节点倒序），结束后 tree_ 为空；
  //   - 再设置新 tree_，非空时 propagateEnterTree()（前序）。
  void Node::setTree(SceneTree* tree) {
    if (tree == tree_) {
      return;
    }
    if (tree_ != nullptr) {
      propagateExitTree();
    }
    tree_ = tree;
    if (tree != nullptr) {
      propagateEnterTree();
    }
  }

  // 前序：当前节点已持有正确 tree_，先触发自身，再正序递归直接 children。
  void Node::propagateEnterTree() {
    YR_ASSERT_MSG(tree_ != nullptr, "Node::propagateEnterTree: node is not inside a tree");
    onEnterTree();
    for (const std::unique_ptr<Node>& child : children_) {
      child->setTree(tree_);
    }
  }

  // 后序且子节点倒序：先从最后一个 child 递归离开，再触发自身，最后才清 tree_。
  void Node::propagateExitTree() {
    YR_ASSERT_MSG(tree_ != nullptr, "Node::propagateExitTree: node is not inside a tree");
    for (std::size_t i = children_.size(); i > 0; --i) {
      children_[i - 1]->setTree(nullptr);
    }
    onExitTree();
    tree_ = nullptr; // 回调结束后才离开树
  }

  void Node::onEnterTree() {}

  // TODO(Node-2 后续): ready 接线后序、默认只一次；本步仅提供 no-op 占位。
  void Node::onReady() {}

  void Node::onUpdate(float /*deltaTime*/) {}

  void Node::onExitTree() {}

} // namespace yr::scene