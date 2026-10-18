#pragma once

#include <cstddef>
#include <memory>
#include <vector>
#include <yr/core/handle.h>
#include <yr/core/object_id.h>
#include <yr/math/mat4.h>
#include <yr/scene/node.h>
#include <yr/scene/transform_store.h>

namespace yr::scene {

  // SceneTree：独占拥有 root，子树由 Node::children_ 级联拥有。setRoot 成功才接管所有权，
  // 先 enter 再 ready；clearRoot 与析构先 exit 再交还所有权。
  //
  // Transform 由 TransformStore 集中持有，refreshTransforms() 显式刷新；Node 只持有 Handle。
  //
  // requestReady 当前不实现。
  class SceneTree {
  public:
    SceneTree() = default;
    // 先让 root 子树 exit，再析构，不会重复 exit。
    ~SceneTree();

    SceneTree(const SceneTree&) = delete;
    SceneTree& operator=(const SceneTree&) = delete;
    SceneTree(SceneTree&&) = delete;
    SceneTree& operator=(SceneTree&&) = delete;

    // 仅当 root 非空且当前无 root 时成功；成功才接管所有权，失败不消费。
    [[nodiscard]] bool setRoot(std::unique_ptr<Node>&& root);

    // 当前根节点；空树返回 nullptr。
    [[nodiscard]] Node* root() const noexcept { return root_.get(); }

    // 先 exit 再交还所有权并恢复空树；空树返回 nullptr。
    [[nodiscard]] std::unique_ptr<Node> clearRoot() noexcept;

    // 执行本批延迟删除。开始时快照队列；flush 期间新 queueFree 留到下一次 flush。
    void flushDeletions();

    // 前序派发 onUpdate：开始时一次性收集整棵子树的前序 ObjectID 快照。
    void process(float deltaTime);

    // 刷新整棵树的 global 缓存
    void refreshTransforms();

    // 诊断用：TransformStore 中的槽数（含已 detach、尚未释放的节点）。
    [[nodiscard]] std::size_t transformCount() const noexcept;

  private:
    friend class Node;
    void enqueueDeletion(core::ObjectID id) { pendingDeletions_.push_back(id); }

    // transform 槽的转发接口：Node 只经 SceneTree 访问，不直接暴露 TransformStore。
    [[nodiscard]] core::Handle<TransformData> createTransform() { return transformStorage_.create(); }
    void destroyTransform(const core::Handle<TransformData>& handle) noexcept { transformStorage_.destroy(handle); }
    [[nodiscard]] TransformData* transformData(const core::Handle<TransformData>& handle) noexcept {
      return transformStorage_.get(handle);
    }

    // 前序刷新 node 子树。parentRecomputed = 祖先本次是否被重算；parentGlobal = 父的
    // global 缓存（root 为 identity）。
    void refreshHelper(Node* node, bool parentRecomputed, const math::Mat4& parentGlobal);

    // 前序收集 node 子树到 out。
    static void collectPreorder(const Node& node, std::vector<core::ObjectID>& out);

    // root_ 声明在 transformStorage_ 之后，故析构逆序时 root_ 先销毁：~Node 仍需经
    // transformOwner_ 归还槽，所以 transformStorage_ 必须活到最后。
    TransformStore transformStorage_;

    std::unique_ptr<Node> root_;
    std::vector<core::ObjectID> pendingDeletions_;
    // 三安全点互斥的重入标志：分别防 flushDeletions / process / refreshTransforms 重入。
    bool flushing_ = false;
    bool processing_ = false;
    bool refreshing_ = false;
  };

} // namespace yr::scene