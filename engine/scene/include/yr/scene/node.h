#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <yr/core/handle.h>
#include <yr/core/object_id.h>
#include <yr/core/string_id.h>
#include <yr/math/transform.h>
#include <yr/object/object.h>
#include <yr/object/variant.h>

namespace yr::scene {

  class SceneTree;
  struct TransformData;

  // Node
  // 生命周期：enter 前序，ready 后序且一生一次，exit 后序倒序。
  // 回调不得抛异常，不得修改正在传播的 parent；
  // .scn 仍只序列化 position
  // name 可写（setName 驻留到全局 StringInterner

  class Node : public yr::obj::Object {
  public:
    Node() = default;
    ~Node() override;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    [[nodiscard]] Node* parent() const noexcept { return parent_; }
    [[nodiscard]] SceneTree* tree() const noexcept { return tree_; }
    [[nodiscard]] bool isInsideTree() const noexcept { return tree_ != nullptr; }
    [[nodiscard]] core::StringId name() const noexcept { return name_; }

    // 名字驻留到全局 StringInterner；空串变成有效空 StringId
    void setName(std::string_view name);

    [[nodiscard]] const yr::obj::ClassInfo* classInfo() const noexcept override;

    [[nodiscard]] std::size_t childCount() const noexcept { return children_.size(); }

    // 越界时返回 nullptr
    [[nodiscard]] Node* childAt(std::size_t index) const noexcept;

    [[nodiscard]] bool addChild(std::unique_ptr<Node>&& child);

    // 返回值丢掉会立即销毁该子树
    [[nodiscard]] std::unique_ptr<Node> removeChild(Node* child);

    // 返回被摘下子树的所有权
    [[nodiscard]] std::unique_ptr<Node> detach();

    [[nodiscard]] bool isQueuedForDeletion() const noexcept { return queuedForDeletion_; }

    // 延迟删除：只置标记并把 ObjectID 交给 SceneTree
    void queueFree();

    // 节点间组合协议：命名信号 -> 目标节点的方法。
    struct SignalConnection {
      core::StringId signal;
      core::ObjectID target;
      core::StringId method;
    };

    // 目标存 ObjectID，发信号时经 ObjectDB 解析；目标销毁后自动跳过。
    void connect(core::StringId signal, core::ObjectID target, core::StringId method);
    void emitSignal(core::StringId signal, const obj::Variant& payload = {});

    void onSignal(core::StringId method, const obj::Variant& payload);

    [[nodiscard]] std::span<const SignalConnection> connections() const noexcept { return connections_; }

    // 本地 TRS
    [[nodiscard]] const math::Vec3& position() const noexcept { return transform_.position; }
    [[nodiscard]] const math::Quat& rotation() const noexcept { return transform_.rotation; }
    [[nodiscard]] const math::Vec3& scale() const noexcept { return transform_.scale; }
    [[nodiscard]] const math::Transform& transform() const noexcept { return transform_; }

    // 改本地数据并置脏
    void setPosition(const math::Vec3& position);
    void setRotation(const math::Quat& rotation);
    void setScale(const math::Vec3& scale);
    void setTransform(const math::Transform& transform);

    // 上次 refreshTransforms() 的缓存；从未入树返回 identity。
    [[nodiscard]] math::Mat4 globalTransform() const noexcept;

  protected:
    // 生命周期 / 更新钩子：只处理当前节点，不负责递归，由私有传播函数在正确的 tree_ 状态下调用
    // onUpdate 由 SceneTree::process 前序派发，回调中增删 / queueFree 安全
    virtual void onEnterTree();
    virtual void onReady();
    virtual void onUpdate(float deltaTime);
    virtual void onExitTree();

  private:
    friend class SceneTree;

    void setTree(SceneTree* tree);
    void propagateEnterTree();
    void propagateExitTree();

    // 后序传播 ready
    void propagateReady();

    // 入树时保证 transform 槽就绪并标脏
    void acquireTransform(SceneTree* tree);
    void markTransformDirty() noexcept;

    core::StringId name_;
    SceneTree* tree_ = nullptr;
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;

    // 一生只 ready 一次
    bool everReady_ = false;

    // children_ 冻结深度 >0 时本节点的 addChild/removeChild 被拒
    // 嵌套 guard 不能提前解除外层冻结。
    std::uint32_t propagationDepth_ = 0;

    bool queuedForDeletion_ = false;

    static constexpr std::uint32_t kMaxSignalDepth = 8;
    std::vector<SignalConnection> connections_;
    std::uint32_t signalDepth_ = 0;

    core::Handle<TransformData> transformHandle_;
    SceneTree* transformOwner_ = nullptr; // SceneTree 生命周期需要长于 Node
    math::Transform transform_{};
  };

} // namespace yr::scene
