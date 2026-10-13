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

    // 先放入 root_，保证 enter 回调中 root 已由 SceneTree 拥有且能通过 tree() 拿回 this。
    root_ = std::move(root);
    // enter 全部完成后再统一 ready，保证 ready 一定发生在所有 enter 之后。
    root_->setTree(this);
    root_->propagateReady();
    return true;
  }

  std::unique_ptr<Node> SceneTree::clearRoot() noexcept {
    if (root_ != nullptr) {
      // 先 exit，再交还所有权。
      root_->setTree(nullptr);
    }
    return std::move(root_);
  }

  SceneTree::~SceneTree() {
    if (root_ != nullptr) {
      // 先让 root 子树 exit；随后 unique_ptr 析构只负责销毁，不会重复 exit。
      root_->setTree(nullptr);
    }
  }

} // namespace yr::scene