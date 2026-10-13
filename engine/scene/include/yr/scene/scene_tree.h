#pragma once

#include <memory>

#include <yr/scene/node.h>

namespace yr::scene {

  // SceneTree：以 std::unique_ptr<Node> 独占拥有 root，root 的子树由
  // Node::children_ 级联拥有。
  //   - setRoot 成功才接管所有权，先 enter（前序）再 ready（后序、只一次）；
  //   - clearRoot 先 exit 再交还所有权；析构也会先触发 root 子树 exit。
  //
  // TODO: requestReady 当前不实现；TODO(Node-3): queueFree 延迟删除；
  // TODO(Node-4): process 遍历。
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

  private:
    std::unique_ptr<Node> root_;
  };

} // namespace yr::scene