#include <algorithm>
#include <cstdint>
#include <yr/core/assert.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>
#include <yr/scene/transform_store.h>

namespace yr::scene {

  // transform 槽由分配它的 SceneTree 释放；该树必须比本节点活得更久。
  Node::~Node() {
    if (transform_.valid() && transformOwner_ != nullptr) {
      transformOwner_->destroyTransform(transform_);
    }
  }

  namespace {

    // 表示正在遍历某个 Node 自己的 children_ 的 RAII 计数，异常展开也安全。
    class PropagationGuard {
    public:
      explicit PropagationGuard(std::uint32_t& depth) noexcept : depth_(depth) { ++depth_; }
      ~PropagationGuard() { --depth_; }

      PropagationGuard(const PropagationGuard&) = delete;
      PropagationGuard& operator=(const PropagationGuard&) = delete;
      PropagationGuard(PropagationGuard&&) = delete;
      PropagationGuard& operator=(PropagationGuard&&) = delete;

    private:
      std::uint32_t& depth_;
    };

  } // namespace

  bool Node::addChild(std::unique_ptr<Node>&& child) {
    // 必须早于任何状态修改，失败才能不消费参数。
    if (propagationDepth_ > 0) {
      YR_ASSERT_MSG(false, "Node::addChild: cannot modify children while this node's children are being propagated");
      return false;
    }

    Node* const raw = child.get();

    if (raw == nullptr) {
      YR_ASSERT_MSG(false, "Node::addChild: child is null");
      return false;
    }
    if (raw == this) {
      YR_ASSERT_MSG(false, "Node::addChild: cannot add self as a child");
      return false;
    }
    if (raw->parent_ != nullptr) {
      YR_ASSERT_MSG(false, "Node::addChild: child already has a parent");
      return false;
    }
    for (Node* ancestor = this; ancestor != nullptr; ancestor = ancestor->parent_) {
      if (ancestor == raw) {
        YR_ASSERT_MSG(false, "Node::addChild: would create a cycle");
        return false;
      }
    }

    raw->parent_ = this;
    children_.push_back(std::move(child));

    // 父已在树内：只让新子树 enter，已有节点不重复 enter。
    if (tree_ != nullptr) {
      // 冻结本节点 children_，覆盖会触发回调的 enter
      const PropagationGuard guard(propagationDepth_);
      raw->setTree(tree_);
      // 父已 ready：新子树 enter 完毕后立即 ready。
      if (everReady_) {
        raw->propagateReady();
      }
    }
    return true;
  }

  Node* Node::childAt(std::size_t index) const noexcept {
    if (index >= children_.size()) {
      YR_ASSERT_MSG(false, "Node::childAt: index out of bounds");
      return nullptr;
    }
    return children_[index].get();
  }

  std::unique_ptr<Node> Node::removeChild(Node* child) {
    // 必须早于任何 tree/parent/children 修改，失败时不改变树结构。
    if (propagationDepth_ > 0) {
      YR_ASSERT_MSG(false, "Node::removeChild: cannot modify children while this node's children are being propagated");
      return nullptr;
    }
    // 冻结本节点 children_，覆盖会触发回调的 exit
    const PropagationGuard guard(propagationDepth_);

    if (child == nullptr) {
      YR_ASSERT_MSG(false, "Node::removeChild: child is null");
      return nullptr;
    }

    const auto it = std::find_if(children_.begin(), children_.end(),
                                 [child](const std::unique_ptr<Node>& entry) { return entry.get() == child; });
    if (it == children_.end()) {
      YR_ASSERT_MSG(false, "Node::removeChild: child does not belong to this parent");
      return nullptr;
    }

    // 先派发 exit 再解除 parent：让回调期间 parent()/isInsideTree() 仍反映旧位置。
    Node* const raw = it->get();
    if (raw->tree_ != nullptr) {
      raw->setTree(nullptr);
    }

    // 先 move 出拥有关系再 erase，避免 erase 后访问已释放的 unique_ptr。
    std::unique_ptr<Node> owned = std::move(*it);
    children_.erase(it);
    owned->parent_ = nullptr;
    owned->queuedForDeletion_ = false; // 交还所有权后不再排队删除
    return owned;
  }

  std::unique_ptr<Node> Node::detach() {
    Node* const oldParent = parent_;
    if (oldParent == nullptr) {
      return nullptr;
    }
    return oldParent->removeChild(this);
  }

  // 树内状态的唯一入口：切换 tree_ 前先对旧树做完整 exit。
  void Node::setTree(SceneTree* tree) {
    if (tree == tree_) {
      return;
    }
    if (tree_ != nullptr) {
      propagateExitTree();
    }
    tree_ = tree;
    if (tree != nullptr) {
      // 先备好本节点的 transform 槽，再 enter；分配不触发回调。
      acquireTransform(tree);
      propagateEnterTree();
    }
  }

  // 同树重新入树保留原槽也必须标脏，否则旧 global 不是新父链下的正确值。
  void Node::acquireTransform(SceneTree* tree) {
    YR_ASSERT_MSG(tree != nullptr, "Node::acquireTransform: tree is null");
    if (transformOwner_ != tree) {
      // 跨树重新挂入：先归还旧树的槽，再在新树分配。
      if (transform_.valid() && transformOwner_ != nullptr) {
        transformOwner_->destroyTransform(transform_);
      }
      transform_ = tree->createTransform();
      transformOwner_ = tree;
    }
    TransformData* const data = tree->transformData(transform_);
    YR_ASSERT_MSG(data != nullptr, "Node::acquireTransform: transform slot is not resolvable");
    if (data != nullptr) {
      data->localDirty = true;
    }
  }

  void Node::setPosition(const math::Vec3& position) {
    position_ = position;
    // 未入树只改本地；有槽则标脏，交给下一次 refresh。
    if (transform_.valid() && transformOwner_ != nullptr) {
      TransformData* const data = transformOwner_->transformData(transform_);
      if (data != nullptr) {
        data->localDirty = true;
      }
    }
  }

  math::Mat4 Node::globalTransform() const noexcept {
    if (transform_.valid() && transformOwner_ != nullptr) {
      const TransformData* const data = transformOwner_->transformData(transform_);
      if (data != nullptr) {
        return data->global;
      }
    }
    return math::Mat4{};
  }

  // guard 只包住 children 遍历：自己的 onEnterTree 仍可 addChild 到 *this，但 child 的
  // enter 回调不能修改正在被遍历的 *this。
  void Node::propagateEnterTree() {
    YR_ASSERT_MSG(tree_ != nullptr, "Node::propagateEnterTree: node is not inside a tree");
    onEnterTree();
    const PropagationGuard guard(propagationDepth_);
    for (const std::unique_ptr<Node>& child : children_) {
      child->setTree(tree_);
    }
  }

  // guard 覆盖 children 倒序遍历与自身 onExitTree：退出期间 *this 结构冻结，增删子节点失败。
  void Node::propagateExitTree() {
    YR_ASSERT_MSG(tree_ != nullptr, "Node::propagateExitTree: node is not inside a tree");
    {
      const PropagationGuard guard(propagationDepth_);
      for (std::size_t i = children_.size(); i > 0; --i) {
        children_[i - 1]->setTree(nullptr);
      }
      onExitTree();
    }
    tree_ = nullptr; // 回调与 guard 结束后才离开树
  }

  // 已 ready 也不能提前返回：其下可能挂有离线新增、从未 ready 的后代，必须继续递归。
  // 先置位 everReady_，使子回调里动态挂入的 child 判断父状态时一致。
  void Node::propagateReady() {
    YR_ASSERT_MSG(tree_ != nullptr, "Node::propagateReady: node is not inside a tree");
    if (tree_ == nullptr) {
      return;
    }

    const bool shouldNotify = !everReady_;
    everReady_ = true;

    // guard 只包住 children 遍历：child 的 ready 回调不能修改正在被遍历的 *this；
    // guard 结束后 onReady() 中才可 addChild。
    {
      const PropagationGuard guard(propagationDepth_);
      for (const std::unique_ptr<Node>& child : children_) {
        child->propagateReady();
      }
    }

    // ready 回调要求节点仍在树内。
    if (shouldNotify) {
      YR_ASSERT_MSG(tree_ != nullptr, "Node::propagateReady: node left the tree during ready propagation");
      if (tree_ != nullptr) {
        onReady();
      }
    }
  }

  void Node::queueFree() {
    if (queuedForDeletion_) {
      return; // 幂等
    }
    // 非树内节点所有权在外部，断言后安全返回，不置标记、不入队。
    YR_ASSERT_MSG(tree_ != nullptr, "Node::queueFree: node is not inside a tree");
    if (tree_ == nullptr) {
      return;
    }
    queuedForDeletion_ = true;
    tree_->enqueueDeletion(id());
  }

  void Node::onEnterTree() {}

  void Node::onReady() {}

  void Node::onUpdate(float /*deltaTime*/) {}

  void Node::onExitTree() {}

} // namespace yr::scene