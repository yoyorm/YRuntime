#include <yr/scene/scene_tree.h>

#include <yr/core/assert.h>

namespace yr::scene {

  bool SceneTree::setRoot(std::unique_ptr<Node>&& root) {
    if (root == nullptr) {
      YR_ASSERT_MSG(false, "SceneTree::setRoot: root is null");
      return false;
    }
    if (root_ != nullptr) {
      YR_ASSERT_MSG(false, "SceneTree::setRoot: tree already has a root");
      return false;
    }

    // 先把所有权放进 root_，保证 enter 回调中 root 已由 SceneTree 拥有，
    // 且回调里能通过 tree() 拿回 this。
    root_ = std::move(root);
    root_->setTree(this);
    return true;
  }

  std::unique_ptr<Node> SceneTree::clearRoot() noexcept {
    if (root_ != nullptr) {
      // 先 exit（后序、子节点倒序），再交还所有权。
      root_->setTree(nullptr);
    }
    return std::move(root_);
  }

  SceneTree::~SceneTree() {
    if (root_ != nullptr) {
      // 先让 root 子树 exit；setTree(nullptr) 会把 tree_ 清空，
      // 随后 unique_ptr 析构只负责销毁，不会再触发一次 exit。
      root_->setTree(nullptr);
    }
  }

} // namespace yr::scene