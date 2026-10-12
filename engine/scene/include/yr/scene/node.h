#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <yr/core/string_id.h>
#include <yr/object/object.h>

namespace yr::scene {

  // Node-1：只负责父子关系与所有权。
  //
  // 本阶段不实现（后续子阶段各自引入）：
  //   - SceneTree / 树内状态（is_inside_tree / tree）
  //   - enter / ready / exit 的真实派发（Node-2）
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

    // TODO(Node-2+): 当前只读、无 setter，名字唯一性尚未实现（不属于 Node-1）。
    [[nodiscard]] core::StringId name() const noexcept { return name_; }

    [[nodiscard]] std::size_t childCount() const noexcept { return children_.size(); }

    // 越界时返回 nullptr（Release 下不做断言也安全）。
    [[nodiscard]] Node* childAt(std::size_t index) const noexcept;

    // 转移所有权给 *this。
    // 参数为右值引用：成功时从中 move 走所有权；
    // 失败时保持调用者的 unique_ptr 不变
    [[nodiscard]] bool addChild(std::unique_ptr<Node>&& child);

    // 交出所有权：把 child 从 *this 的子列表中摘出并返回其 unique_ptr，
    // 同时清空 child->parent_。child 不是 *this 的直接子节点时返回 nullptr。
    // 返回值必须被接住，丢掉它会立即销毁该子树。
    [[nodiscard]] std::unique_ptr<Node> removeChild(Node* child);

    // 把自己从父节点摘下，返回所有权；自己没有父节点时返回 nullptr。
    [[nodiscard]] std::unique_ptr<Node> detach();

    // TODO(Node-2/Node-4): 生命周期 / 更新回调。
    // Node-1 只保留占位声明与默认 no-op，**不做任何递归派发**；
    // 实际触发时机与顺序（enter 前序 / ready 后序且只一次 / exit 后序 /
    virtual void onUpdate(float deltaTime);
    virtual void onEnterTree();
    virtual void onExitTree();

  private:
    core::StringId name_;
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
  };

} // namespace yr::scene