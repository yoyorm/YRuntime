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

  void Node::onUpdate(float /*deltaTime*/) {}

  void Node::onEnterTree() {}

  void Node::onExitTree() {}

} // namespace yr::scene