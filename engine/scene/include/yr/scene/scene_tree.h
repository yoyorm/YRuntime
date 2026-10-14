#pragma once

#include "yr/core/object_id.h"
#include <memory>

#include <vector>
#include <yr/scene/node.h>

namespace yr::scene {

  // SceneTree：以 std::unique_ptr<Node> 独占拥有 root，root 的子树由
  // Node::children_ 级联拥有。
  //   - setRoot 成功才接管所有权，先 enter（前序）再 ready（后序、只一次）；
  //   - clearRoot 先 exit 再交还所有权；析构也会先触发 root 子树 exit。
  //
  // TODO: requestReady 当前不实现；TODO(Node-4): process 遍历。
  class SceneTree {
  public:
    SceneTree() = default;
    // 先让 root 整棵子树 exit，再由 unique_ptr 析构，不会重复 exit。
    ~SceneTree();

    SceneTree(const SceneTree&) = delete;
    SceneTree& operator=(const SceneTree&) = delete;
    SceneTree(SceneTree&&) = delete;
    SceneTree& operator=(SceneTree&&) = delete;

    // 仅当 root 非空且当前没有 root 时成功；成功才接管所有权，失败不消费参数。
    [[nodiscard]] bool setRoot(std::unique_ptr<Node>&& root);

    // 当前根节点；空树返回 nullptr。
    [[nodiscard]] Node* root() const noexcept { return root_.get(); }

    // 先 exit 再交还所有权并恢复空树；空树返回 nullptr。
    [[nodiscard]] std::unique_ptr<Node> clearRoot() noexcept;

    // 执行本批延迟删除。开始时把队列 swap 到局部（快照语义），本批只处理这一批；
    // flush 期间（含 exit 回调里）新 queueFree 的请求留到下一次 flush，因此天然有界。
    // 必须在遍历之外的安全点调用。
    void flushDeletions();

  private:
    friend class Node;
    void enqueueDeletion(core::ObjectID id) { pendingDeletions_.push_back(id); }

    std::unique_ptr<Node> root_;
    // 存 ObjectID 而非 Node*：节点可能已被级联删除，flush 时经 ObjectDB generation 解析失败即跳过。
    std::vector<core::ObjectID> pendingDeletions_;
    // flush 重入防护：exit 回调里再调 flushDeletions 会让 removeChild 正持有的迭代器失效。
    bool flushing_ = false;
  };

} // namespace yr::scene