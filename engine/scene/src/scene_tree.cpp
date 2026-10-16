#include <memory>
#include <vector>

#include <yr/core/assert.h>
#include <yr/object/object_db.h>
#include <yr/scene/scene_tree.h>

namespace yr::scene {

  namespace {

    // 纯平移矩阵：column-major 下平移量在索引 12/13/14。
    [[nodiscard]] math::Mat4 makeTranslation(const math::Vec3& position) noexcept {
      math::Mat4 result;
      result[12] = position.x;
      result[13] = position.y;
      result[14] = position.z;
      return result;
    }

  } // namespace

  bool SceneTree::setRoot(std::unique_ptr<Node>&& root) {
    if (root == nullptr) {
      YR_ASSERT_MSG(false, "SceneTree::setRoot: root is null");
      return false;
    }
    if (root_ != nullptr) {
      YR_ASSERT_MSG(false, "SceneTree::setRoot: tree already has a root");
      return false;
    }

    // 先放入 root_，保证 enter 回调中 root 已由 SceneTree 拥有且能拿回 tree()。
    root_ = std::move(root);
    // 所有 enter 完成后再统一 ready。
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
      // 先让 root 子树 exit，再在 transformStorage_ 仍存活时销毁，保证 ~Node 能归还槽。
      root_->setTree(nullptr);
      root_.reset();
    }
  }

  void SceneTree::flushDeletions() {
    // 必须在遍历之外的安全点调用。重入会让 removeChild 持有的迭代器失效：断言 + 早退，
    // 新请求留给下一次 flush。
    YR_ASSERT_MSG(!flushing_, "SceneTree::flushDeletions: recursive flush is not allowed");
    if (flushing_) {
      return;
    }
    // process / refreshTransforms 期间不得 flush：三个安全点互斥
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

    // 快照：把待删队列换出到局部，本批只处理这一批。flush 期间新 queueFree 写入已清空
    // 的 pendingDeletions_，留给下一次 flush。
    std::vector<core::ObjectID> batch;
    batch.swap(pendingDeletions_);

    for (const core::ObjectID& id : batch) {
      // 父先删时子节点已随父销毁，get 返回 nullptr。
      Node* node = static_cast<Node*>(obj::globalObjectDB().get(id));
      if (node == nullptr) {
        continue;
      }
      // 节点已被 removeChild/detach（标志复位）或又挂回树，不能删除，静默跳过。
      if (!node->isQueuedForDeletion()) {
        continue;
      }

      if (node->parent_ != nullptr) {
        // 子节点：detach 交出所有权，局部 unique_ptr 作用域结束即析构整棵子树；
        // guard 非零等情况下可能返回 nullptr，安全跳过。
        std::unique_ptr<Node> deletion = node->detach();
        (void)deletion;
      } else if (node == root_.get()) {
        // 根节点：先 exit 再交还所有权，由局部 unique_ptr 析构。
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
    // 重入会让同一帧重复派发：断言 + 早退。
    YR_ASSERT_MSG(!processing_, "SceneTree::process: recursive process is not allowed");
    if (processing_) {
      return;
    }
    // refreshTransforms 期间不得 process：三个安全点互斥。
    YR_ASSERT_MSG(!refreshing_, "SceneTree::process: cannot process while refreshing transforms");
    if (refreshing_) {
      return;
    }

    // RAII 恢复 processing_，回调抛异常也不卡住标志。
    struct ProcessGuard {
      bool& flag;
      explicit ProcessGuard(bool& value) noexcept : flag(value) { flag = true; }
      ~ProcessGuard() { flag = false; }
    };
    ProcessGuard guard(processing_);

    if (root_ == nullptr) {
      return; // 空树安全 no-op
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
    // 与 process / flushDeletions 互斥；重入只是重复遍历，仍断言 + 早退。
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

    // RAII 恢复 refreshing_。
    struct RefreshGuard {
      bool& flag;
      explicit RefreshGuard(bool& value) noexcept : flag(value) { flag = true; }
      ~RefreshGuard() { flag = false; }
    };
    RefreshGuard guard(refreshing_);

    if (root_ == nullptr) {
      return; // 空树安全 no-op
    }

    // root 无父：父矩阵为 identity，parentRecomputed 为 false。
    refreshHelper(root_.get(), false, math::Mat4{});
    (void)guard;
  }

  void SceneTree::refreshHelper(Node* node, bool parentRecomputed, const math::Mat4& parentGlobal) {
    TransformData* const data = transformData(node->transform_);
    YR_ASSERT_MSG(data != nullptr, "SceneTree::refreshHelper: node has no transform slot");
    if (data == nullptr) {
      return;
    }

    // global = parentGlobal * T(position)，仅当本地变过或祖先被重算。
    const bool recompute = parentRecomputed || data->localDirty;
    if (recompute) {
      data->global = parentGlobal * makeTranslation(node->position_);
      data->localDirty = false;
    }

    // 子树被重算则整体重算，否则各自看 localDirty。
    for (const std::unique_ptr<Node>& child : node->children_) {
      refreshHelper(child.get(), recompute, data->global);
    }
  }

  std::size_t SceneTree::transformCount() const noexcept {
    return transformStorage_.size();
  }

} // namespace yr::scene