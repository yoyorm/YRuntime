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
  // TODO: requestReady 当前不实现。
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

    // 执行本批延迟删除。开始时把队列快照保存；
    // flush 期间（含 exit 回调里）新 queueFree 的请求留到下一次 flush
    // 必须在遍历之外的安全点调用。process 期间被拒（对称保护），新删除留到下一次 flush。
    void flushDeletions();

    // 前序派发 onUpdate：开始时一次性收集整棵子树的前序 ObjectID 快照，派发阶段逐个
    // 经 ObjectDB 重解析——解析失败（已级联销毁/注销）或已不在树内（回调中 detach）
    // 的条目跳过。因此回调中 removeChild/addChild/queueFree 都安全：新增节点本帧不入
    // 快照、下一帧才收集到，删除节点靠 ID 失效容忍，无需冻结结构。
    // 重入被拒（assert + 早退）；与 flushDeletions 分离，由调用方按帧阶段先后调用。
    void process(float deltaTime);

  private:
    friend class Node;
    void enqueueDeletion(core::ObjectID id) { pendingDeletions_.push_back(id); }

    // 前序收集 node 子树：先本节点 id()，再正序递归 children。纯遍历，无回调。
    static void collectPreorder(const Node& node, std::vector<core::ObjectID>& out);

    std::unique_ptr<Node> root_;
    // 存 ObjectID 而非 Node*：节点可能已被级联删除，flush 时经 ObjectDB generation 解析失败即跳过。
    std::vector<core::ObjectID> pendingDeletions_;
    // flush 重入防护：exit 回调里再调 flushDeletions 会让 removeChild 正持有的迭代器失效。
    bool flushing_ = false;
    // process 重入防护；同时让 process 期间的 flushDeletions 早退，保证两个安全点互斥。
    bool processing_ = false;
  };

} // namespace yr::scene