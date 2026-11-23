#include <memory>
#include <vector>

#include <yr/core/assert.h>
#include <yr/object/object_db.h>
#include <yr/scene/scene_tree.h>

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

    root_ = std::move(root);
    // 所有 enter 完成后再统一 ready
    root_->setTree(this);
    root_->propagateReady();
    // 首个 process 阶段之前先种好 global 缓存：否则行为节点在首帧读到的世界坐标全是原点。
    refreshTransforms();
    return true;
  }

  std::unique_ptr<Node> SceneTree::clearRoot() noexcept {
    if (root_ != nullptr) {
      root_->setTree(nullptr);
    }
    return std::move(root_);
  }

  SceneTree::~SceneTree() {
    if (root_ != nullptr) {
      root_->setTree(nullptr);
      root_.reset();
    }
  }

  void SceneTree::flushDeletions() {
    YR_ASSERT_MSG(!flushing_, "SceneTree::flushDeletions: recursive flush is not allowed");
    if (flushing_) {
      return;
    }
    YR_ASSERT_MSG(!processing_, "SceneTree::flushDeletions: cannot flush while processing");
    if (processing_) {
      return;
    }
    YR_ASSERT_MSG(!refreshing_, "SceneTree::flushDeletions: cannot flush while refreshing transforms");
    if (refreshing_) {
      return;
    }

    // RAII 恢复 flushing_
    struct FlushGuard {
      bool& flag;
      explicit FlushGuard(bool& value) noexcept : flag(value) { flag = true; }
      ~FlushGuard() { flag = false; }
    };
    FlushGuard guard(flushing_);

    // 快照：把待删队列换出到局部，本批只处理这一批
    // 新请求留给下一次 flush。
    std::vector<core::ObjectID> batch;
    batch.swap(pendingDeletions_);

    for (const core::ObjectID& id : batch) {
      // 父先删时子节点已随父销毁，get 返回 nullptr。
      Node* node = static_cast<Node*>(obj::globalObjectDB().get(id));
      if (node == nullptr) {
        continue;
      }
      // 节点已被 removeChild/detach
      if (!node->isQueuedForDeletion()) {
        continue;
      }

      if (node->parent_ != nullptr) {
        std::unique_ptr<Node> deletion = node->detach();
        (void)deletion;
      } else if (node == root_.get()) {
        std::unique_ptr<Node> deletion = clearRoot();
        (void)deletion;
      } else {
        // 无父又非 root 的孤儿节点（所有权在外部），不删除；断言后解除标记避免重复断言。
        YR_ASSERT_MSG(false, "SceneTree::flushDeletions: queued node is detached and not the root; skipping");
        node->queuedForDeletion_ = false;
      }
    }
    (void)guard;
  }

  void SceneTree::collectPreorder(const Node& node, std::vector<core::ObjectID>& out) {
    out.push_back(node.id());
    for (const std::unique_ptr<Node>& child : node.children_) {
      collectPreorder(*child, out);
    }
  }

  void SceneTree::process(float deltaTime) {
    YR_ASSERT_MSG(!processing_, "SceneTree::process: recursive process is not allowed");
    if (processing_) {
      return;
    }
    YR_ASSERT_MSG(!refreshing_, "SceneTree::process: cannot process while refreshing transforms");
    if (refreshing_) {
      return;
    }
    YR_ASSERT_MSG(!flushing_, "SceneTree::process: cannot process while flushing deletions");
    if (flushing_) {
      return;
    }

    // RAII 恢复 processing_
    struct ProcessGuard {
      bool& flag;
      explicit ProcessGuard(bool& value) noexcept : flag(value) { flag = true; }
      ~ProcessGuard() { flag = false; }
    };
    ProcessGuard guard(processing_);

    if (root_ == nullptr) {
      return;
    }

    // 快照：本帧开始时一次性前序收集整棵子树
    std::vector<core::ObjectID> snapshot;
    collectPreorder(*root_, snapshot);

    for (const core::ObjectID& id : snapshot) {
      Node* node = static_cast<Node*>(obj::globalObjectDB().get(id));
      if (node == nullptr) {
        continue;
      }
      if (!node->isInsideTree()) {
        continue;
      }
      node->onUpdate(deltaTime);
    }
    (void)guard;
  }

  void SceneTree::refreshTransforms() {
    YR_ASSERT_MSG(!refreshing_, "SceneTree::refreshTransforms: recursive refresh is not allowed");
    if (refreshing_) {
      return;
    }
    YR_ASSERT_MSG(!processing_, "SceneTree::refreshTransforms: cannot refresh while processing");
    if (processing_) {
      return;
    }
    YR_ASSERT_MSG(!flushing_, "SceneTree::refreshTransforms: cannot refresh while flushing deletions");
    if (flushing_) {
      return;
    }

    // RAII 恢复 refreshing_
    struct RefreshGuard {
      bool& flag;
      explicit RefreshGuard(bool& value) noexcept : flag(value) { flag = true; }
      ~RefreshGuard() { flag = false; }
    };
    RefreshGuard guard(refreshing_);

    if (root_ == nullptr) {
      return;
    }

    refreshHelper(root_.get(), false, math::Mat4{});
    (void)guard;
  }

  void SceneTree::refreshHelper(Node* node, bool parentRecomputed, const math::Mat4& parentGlobal) {
    TransformData* const data = transformData(node->transformHandle_);
    YR_ASSERT_MSG(data != nullptr, "SceneTree::refreshHelper: node has no transform slot");
    if (data == nullptr) {
      return;
    }

    const bool recompute = parentRecomputed || data->localDirty;
    if (recompute) {
      // global = parentGlobal * T*R*S
      data->global = parentGlobal * math::toMat4(node->transform_);
      data->localDirty = false;
    }

    for (const std::unique_ptr<Node>& child : node->children_) {
      refreshHelper(child.get(), recompute, data->global);
    }
  }

  std::size_t SceneTree::transformCount() const noexcept {
    return transformStorage_.size();
  }

} // namespace yr::scene
