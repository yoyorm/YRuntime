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

  void SceneTree::flushDeletions() {
    // flush 必须在遍历之外的安全点调用。exit 回调里重入 flush 会让 removeChild 正持有
    // 的迭代器失效：与 EventBus 一致，断言 + 早退，重入期间的新请求留给下一次 flush。
    YR_ASSERT_MSG(!flushing_, "SceneTree::flushDeletions: recursive flush is not allowed");
    if (flushing_) {
      return;
    }

    // RAII 恢复 flushing_，回调若违约抛异常也不会卡住标志。
    struct FlushGuard {
      bool& flag;
      explicit FlushGuard(bool& value) noexcept : flag(value) { flag = true; }
      ~FlushGuard() { flag = false; }
    };
    FlushGuard guard(flushing_);

    // 快照：把当前待删队列换出到局部，本批只处理这一批。flush 期间 exit 回调里
    // 再次 queueFree 只写入已清空的 pendingDeletions_，留给下一次 flush，天然有界。
    std::vector<core::ObjectID> batch;
    batch.swap(pendingDeletions_);

    for (const core::ObjectID& id : batch) {
      // 父先删时子节点已随父销毁，ObjectDB 的 generation 已失效，get 返回 nullptr。
      Node* node = static_cast<Node*>(obj::globalObjectDB().get(id));
      if (node == nullptr) {
        continue;
      }
      // 陈旧条目：节点已被手动 removeChild/detach（= 取消删除意图，标志已复位）
      // 或又挂回了树，此时绝不能删除，静默跳过。
      if (!node->isQueuedForDeletion()) {
        continue;
      }

      if (node->parent_ != nullptr) {
        // 子节点：removeChild 交出所有权，局部 unique_ptr 作用域结束即析构整棵子树。
        // detach 在 guard 非零等情况下返回 nullptr，此时安全跳过、不崩溃。
        std::unique_ptr<Node> deletion = node->detach();
        (void)deletion;
      } else if (node == root_.get()) {
        // 根节点：先 exit 再交还所有权，由局部 unique_ptr 析构。
        std::unique_ptr<Node> deletion = clearRoot();
        (void)deletion;
      } else {
        // 无父又非 root 且仍带删除标记：不该出现的孤儿节点（所有权在外部），
        // 绝不删除；断言诊断后解除标记，避免下一次 flush 重复断言。
        YR_ASSERT_MSG(false, "SceneTree::flushDeletions: queued node is detached and not the root; skipping");
        node->queuedForDeletion_ = false;
      }
    }
    (void)guard;
  }

} // namespace yr::scene