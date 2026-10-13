#pragma once

#include <memory>

#include <yr/scene/node.h>

namespace yr::scene {

  // Node-2 的 enter/exit 子步骤：SceneTree 拥有 root，并在 setRoot/clearRoot/
  // 析构时通过 Node::setTree 驱动整棵树 enter/exit 的递归传播。
  //
  // 本步明确不实现（由后续子阶段各自引入）：
  //   - ready 状态与传播（Node-2 后续，后序且只一次）
  //   - blocked / 回调中结构修改的完整重入保护
  //   - 延迟删除：queueFree + ObjectID 删除队列 + flushDeletions（Node-3）
  //   - process / physicsProcess 遍历（Node-4）
  //
  // 所有权语义：
  //   - SceneTree 通过 std::unique_ptr<Node> 拥有整棵树的根；
  //     根的子树由 Node::children_ 里的 unique_ptr 级联拥有。
  class SceneTree {
  public:
    SceneTree() = default;
    // 先让 root 整棵子树 exit，再由 unique_ptr 析构（不会重复 exit）。
    ~SceneTree();

    SceneTree(const SceneTree&) = delete;
    SceneTree& operator=(const SceneTree&) = delete;
    SceneTree(SceneTree&&) = delete;
    SceneTree& operator=(SceneTree&&) = delete;

    // 设置根节点：仅当 root 非空、且当前没有 root 时成功。
    // 成功才从参数 move 走所有权并以 enter（前序）触发 root 子树；失败不消费。
    [[nodiscard]] bool setRoot(std::unique_ptr<Node>&& root);

    // 当前根节点；空树返回 nullptr。
    [[nodiscard]] Node* root() const noexcept { return root_.get(); }

    // 交还 root 所有权并恢复空树；空树返回 nullptr。
    // 先以 exit（后序、子节点倒序）让 root 子树离开，再 move 出 root。
    [[nodiscard]] std::unique_ptr<Node> clearRoot() noexcept;

  private:
    std::unique_ptr<Node> root_;
  };

} // namespace yr::scene