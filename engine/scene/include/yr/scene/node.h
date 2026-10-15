#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <yr/core/string_id.h>
#include <yr/object/object.h>

namespace yr::scene {

  class SceneTree;

  // Node：父子关系与 enter/ready/exit 生命周期传播。
  //
  // 所有权：
  //   - parent 以 std::vector<std::unique_ptr<Node>> 独占拥有 children；
  //   - parent_ 是非拥有裸指针，仅在节点仍挂在父列表中时有效；
  //   - addChild 成功才转移所有权，失败不消费（调用者仍持有 child）。
  //
  // 生命周期：
  //   - enter 前序（父先子），ready 后序（子先父）且默认一生只回调一次，exit 后序且子节点倒序；
  //   - enter/ready 回调中允许当前节点添加自己的 child，但不得修改正在传播的 parent；
  //   - exit 传播期间当前节点的结构冻结；
  //   - addChild/removeChild/detach 是结构操作，期间接收者自身的 children_ 被冻结：
  //     操作触发的回调里对该接收者的任何增删子节点都会被拒（断言 + 返回失败），
  //     从而保证外层遍历 / 迭代器始终有效。exit 的 parent()/isInsideTree() 语义不变。
  //
  // TODO: name 目前只读，名字唯一性尚未实现。

  class Node : public yr::obj::Object {
  public:
    Node() = default;
    ~Node() override = default;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    // 非拥有；无父（树根或已被摘下）时为 nullptr。
    [[nodiscard]] Node* parent() const noexcept { return parent_; }

    [[nodiscard]] SceneTree* tree() const noexcept { return tree_; }
    [[nodiscard]] bool isInsideTree() const noexcept { return tree_ != nullptr; }

    [[nodiscard]] core::StringId name() const noexcept { return name_; }

    [[nodiscard]] std::size_t childCount() const noexcept { return children_.size(); }

    // 越界时返回 nullptr。
    [[nodiscard]] Node* childAt(std::size_t index) const noexcept;

    [[nodiscard]] bool addChild(std::unique_ptr<Node>&& child);

    // 返回值必须接住，丢掉会立即销毁该子树。
    [[nodiscard]] std::unique_ptr<Node> removeChild(Node* child);

    // 返回被摘下子树的所有权。
    [[nodiscard]] std::unique_ptr<Node> detach();

    [[nodiscard]] bool isQueuedForDeletion() const noexcept { return queuedForDeletion_; }

    // 延迟删除：只置标记并把自己的 ObjectID 交给所属 SceneTree，不触发回调、不改结构。
    // 不在树内是编程错误（所有权在外部），Debug 断言、所有构建安全返回；重复调用幂等。
    void queueFree();

  protected:
    // 生命周期 / 更新钩子：只处理当前节点，不负责递归；由私有传播函数在正确的
    // tree_ 状态下调用。回调契约：
    //   - 不得抛异常；
    //   - onEnterTree / onReady 可以修改自己的 children，但不得修改正在传播的 parent；
    //   - onExitTree 期间结构冻结，不得增删子节点。
    // onUpdate 由 SceneTree::process 前序派发（父先子、兄弟按 children 顺序），
    // 回调中允许 addChild / removeChild / queueFree，安全由 ObjectID 快照重解析保证。
    virtual void onEnterTree();
    virtual void onReady();
    virtual void onUpdate(float deltaTime);
    virtual void onExitTree();

  private:
    friend class SceneTree;

    // 进入 / 离开 *this 及其整棵子树；tree == tree_ 时安全 no-op。
    void setTree(SceneTree* tree);
    void propagateEnterTree();
    void propagateExitTree();

    // ready 后序传播（子先父）。已 ready 的节点仍继续递归，让离线新增、从未
    // ready 的后代补上 ready；仅首次 ready 的节点调用 onReady()。
    void propagateReady();

    core::StringId name_;
    SceneTree* tree_ = nullptr;
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
    // 一生只 ready 一次的标记；detach/exit 不重置，重新入树只重新 enter。
    bool everReady_ = false;
    // 本节点 children_ 的传播 / 结构修改冻结深度：>0 时本节点的 addChild /
    // removeChild / detach 一律被拒。用计数而非 bool：嵌套 guard 不能提前解除
    // 外层的冻结。
    std::uint32_t propagationDepth_ = 0;
    // 已入删除队列的标记；只在 queueFree 成功入队时置位，节点销毁时随之消失。
    bool queuedForDeletion_ = false;
  };

} // namespace yr::scene