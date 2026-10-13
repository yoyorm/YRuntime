#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <yr/core/string_id.h>
#include <yr/object/object.h>

namespace yr::scene {

  class SceneTree;

  // Node：父子关系、所有权，以及 enter/exit 生命周期传播。
  //
  // 本阶段已完成（Node-2 的 enter/exit 子步骤）：
  //   - 树内状态：tree_ 反向指针 / tree() / isInsideTree()
  //   - enter（前序，父先子）/ exit（后序，子节点倒序）的递归传播
  //
  // 本阶段不实现（后续子阶段各自引入）：
  //   - ready 状态与传播（Node-2 后续，后序且只一次）
  //   - queueFree + 延迟删除队列（Node-3）
  //   - process 遍历与注册列表快照（Node-4）
  //   - NodePath、名字唯一性、Transform、ClassDB Node 工厂
  //
  // 所有权语义（详见 doc/01-architecture.md §4.6 的所有权矩阵）：
  //   - parent → child：this 通过 std::vector<std::unique_ptr<Node>> 拥有 child；
  //   - child → parent：裸指针，非拥有，仅在 child 存活于父列表期间有效；
  //   - addChild 成功即转移所有权；失败时调用者仍持有 child。

  class Node : public yr::obj::Object {
  public:
    Node() = default;
    ~Node() override = default;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    // 非拥有的父指针。无父（树根或已被摘下）时为 nullptr。
    [[nodiscard]] Node* parent() const noexcept { return parent_; }

    // 获取所属的 SceneTree。
    [[nodiscard]] SceneTree* tree() const noexcept { return tree_; }
    [[nodiscard]] bool isInsideTree() const noexcept { return tree_ != nullptr; }

    // TODO(Node-2+): 当前只读、无 setter，名字唯一性尚未实现（不属于 Node-1）。
    [[nodiscard]] core::StringId name() const noexcept { return name_; }

    [[nodiscard]] std::size_t childCount() const noexcept { return children_.size(); }

    // 越界时返回 nullptr
    [[nodiscard]] Node* childAt(std::size_t index) const noexcept;

    // 参数为右值引用：成功时从中 move 走所有权；
    // 失败时保持调用者的 unique_ptr 不变
    [[nodiscard]] bool addChild(std::unique_ptr<Node>&& child);

    // child 不是 *this 的直接子节点时返回 nullptr。
    // 返回值必须被接住，丢掉它会立即销毁该子树。
    [[nodiscard]] std::unique_ptr<Node> removeChild(Node* child);

    // 把自己从父节点摘下，返回所有权
    [[nodiscard]] std::unique_ptr<Node> detach();

  protected:
    // 生命周期 / 更新钩子：只处理当前节点，**不负责递归**；由 Node 私有传播
    // 函数在正确的 tree_ 状态下调用（enter 前序 / exit 后序，见 node.cpp）。
    // 回调契约：
    //   - 不得抛异常（传播函数不做回滚）；
    //   - 不得修改树结构（addChild/removeChild/detach），否则会破坏传播遍历。
    // Node-2 本步只接线 onEnterTree/onExitTree；ready/update 仍是占位 no-op。
    virtual void onEnterTree();
    virtual void onReady(); // TODO(Node-2 后续): 接线后序、默认只一次
    virtual void onUpdate(float deltaTime);
    virtual void onExitTree();

  private:
    friend class SceneTree;

    // 进入 / 离开 *this 及其整棵子树；tree == tree_ 时安全 no-op。
    // enter 为前序（父先子），exit 为后序且子节点倒序，均由 setTree 驱动。
    void setTree(SceneTree* tree);
    void propagateEnterTree();
    void propagateExitTree();

    core::StringId name_;
    SceneTree* tree_ = nullptr;
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
  };

} // namespace yr::scene