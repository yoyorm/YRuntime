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

  // SceneTree：独占拥有 root
  // setRoot 成功才接管所有权
  // 先 enter 再 ready
  // Transform 由 TransformStore 集中持有，refreshTransforms() 显式刷新

  class SceneTree {
  public:
    SceneTree() = default;
    // 先让 root 子树 exit，再析构
    ~SceneTree();

    SceneTree(const SceneTree&) = delete;
    SceneTree& operator=(const SceneTree&) = delete;
    SceneTree(SceneTree&&) = delete;
    SceneTree& operator=(SceneTree&&) = delete;

    // 仅当 root 非空且当前无 root 时成功；成功才接管所有权，失败不消费
    [[nodiscard]] bool setRoot(std::unique_ptr<Node>&& root);

    // 当前根节点；空树返回 nullptr。
    [[nodiscard]] Node* root() const noexcept { return root_.get(); }

    // 恢复空树；空树返回 nullptr。
    [[nodiscard]] std::unique_ptr<Node> clearRoot() noexcept;

    // 执行本批延迟删除
    void flushDeletions();

    // 前序派发
    void process(float deltaTime);

    // 刷新整棵树的 global 缓存
    void refreshTransforms();

    // 诊断用：TransformStore 中的槽数（含已 detach、尚未释放的节点）
    [[nodiscard]] std::size_t transformCount() const noexcept;

  private:
    friend class Node;
    void enqueueDeletion(core::ObjectID id) { pendingDeletions_.push_back(id); }

    // transform 槽转发接口
    [[nodiscard]] core::Handle<TransformData> createTransform() { return transformStorage_.create(); }

    void destroyTransform(const core::Handle<TransformData>& handle) noexcept { transformStorage_.destroy(handle); }

    [[nodiscard]] TransformData* transformData(const core::Handle<TransformData>& handle) noexcept {
      return transformStorage_.get(handle);
    }

    // 前序刷新 node 子树
    void refreshHelper(Node* node, bool parentRecomputed, const math::Mat4& parentGlobal);

    static void collectPreorder(const Node& node, std::vector<core::ObjectID>& out);

    TransformStore transformStorage_;

    std::unique_ptr<Node> root_;
    std::vector<core::ObjectID> pendingDeletions_;

    // 防止修改重入
    bool flushing_ = false;
    bool processing_ = false;
    bool refreshing_ = false;
  };

} // namespace yr::scene