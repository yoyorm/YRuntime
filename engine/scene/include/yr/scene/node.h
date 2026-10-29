#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <yr/core/handle.h>
#include <yr/core/string_id.h>
#include <yr/math/mat4.h>
#include <yr/math/vec3.h>
#include <yr/object/object.h>

namespace yr::scene {

  class SceneTree;
  struct TransformData;

  // Node：父子层级与 enter/ready/exit 生命周期传播。
  //
  // 所有权：children_ 独占拥有；parent_ 非拥有；addChild 成功才转移所有权，失败不消费。
  //
  // 生命周期：enter 前序，ready 后序且一生一次，exit 后序倒序。回调不得抛异常，不得修改正在
  // 传播的 parent；传播期间接收者的 children_ 冻结，增删子节点被拒（断言 + 失败）。
  //
  // Transform 仅平移；读取与槽生命周期契约见 doc/layers/scene.md。
  //
  // name 可写（setName 驻留到全局 StringInterner）；唯一性由 scene loader 校验，Node 不强制。

  class Node : public yr::obj::Object {
  public:
    Node() = default;
    ~Node() override;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    // 非拥有；无父（树根或已被摘下）时为 nullptr。
    [[nodiscard]] Node* parent() const noexcept { return parent_; }

    [[nodiscard]] SceneTree* tree() const noexcept { return tree_; }
    [[nodiscard]] bool isInsideTree() const noexcept { return tree_ != nullptr; }

    [[nodiscard]] core::StringId name() const noexcept { return name_; }

    // 名字驻留到全局 StringInterner；空串变成有效空 StringId
    void setName(std::string_view name);

    [[nodiscard]] const yr::obj::ClassInfo* classInfo() const noexcept override;

    [[nodiscard]] std::size_t childCount() const noexcept { return children_.size(); }

    // 越界时返回 nullptr。
    [[nodiscard]] Node* childAt(std::size_t index) const noexcept;

    [[nodiscard]] bool addChild(std::unique_ptr<Node>&& child);

    // 返回值必须接住，丢掉会立即销毁该子树。
    [[nodiscard]] std::unique_ptr<Node> removeChild(Node* child);

    // 返回被摘下子树的所有权。
    [[nodiscard]] std::unique_ptr<Node> detach();

    [[nodiscard]] bool isQueuedForDeletion() const noexcept { return queuedForDeletion_; }

    // 延迟删除：只置标记并把 ObjectID 交给 SceneTree，不改结构。不在树内属编程错误
    // （所有权在外部），断言后安全返回；重复调用幂等。
    void queueFree();

    // 本地 position（父坐标系下的平移）；默认 (0,0,0)。
    [[nodiscard]] const math::Vec3& position() const noexcept { return position_; }

    // O(1)：只改本地数据并置脏，不传播、不触发回调。离线同样可写，position_ 是唯一数据源。
    void setPosition(const math::Vec3& position);

    // 上一次 refreshTransforms() 的缓存；从未入树返回 identity。
    [[nodiscard]] math::Mat4 globalTransform() const noexcept;

  protected:
    // 生命周期 / 更新钩子：只处理当前节点，不负责递归，由私有传播函数在正确的 tree_ 状态下
    // 调用。onUpdate 由 SceneTree::process 前序派发，回调中增删 / queueFree 安全（ObjectID 快照重解析）。
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

    // 后序传播 ready。已 ready 仍递归，让离线新增、从未 ready 的后代补上 ready；仅首次调用 onReady()。
    void propagateReady();

    // 入树时保证 transform 槽就绪并标脏（同树重新入树也要标脏，使新父链生效）。
    void acquireTransform(SceneTree* tree);

    core::StringId name_;
    SceneTree* tree_ = nullptr;
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
    // 一生只 ready 一次的标记；detach/exit 不重置，重新入树只重新 enter。
    bool everReady_ = false;
    // children_ 冻结深度：>0 时本节点的 addChild/removeChild 被拒（子节点的 detach 经此，
    // 同样被拒）。用计数而非 bool，嵌套 guard 不能提前解除外层冻结。
    std::uint32_t propagationDepth_ = 0;
    // 已入删除队列的标记；queueFree 成功入队时置位。
    bool queuedForDeletion_ = false;

    // transform 槽在所属 SceneTree 的 TransformStore 中，首次入树分配、detach 保留、
    // Node 析构释放。transformOwner_ 是分配该槽的 SceneTree（非拥有），detach 后仍用于
    // 查询旧 global 与释放。契约：SceneTree 必须比持有其 handle 的 Node 活得更久。
    core::Handle<TransformData> transform_;
    SceneTree* transformOwner_ = nullptr;
    // position_ 是唯一可写数据源，离线也可写。
    math::Vec3 position_{};
  };

} // namespace yr::scene