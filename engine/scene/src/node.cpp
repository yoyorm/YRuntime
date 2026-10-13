#include <yr/scene/node.h>

#include <algorithm>
#include <cstdint>

#include <yr/core/assert.h>

namespace yr::scene {

  namespace {

    // 只表示"正在遍历某个 Node 自己的 children_"的 RAII 计数：构造递增、析构递减，
    // 异常展开也安全；只持有计数器引用，无需访问 Node 私有成员。
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
    // 沿 parent 链向上检查，防止形成环。
    for (Node* ancestor = this; ancestor != nullptr; ancestor = ancestor->parent_) {
      if (ancestor == raw) {
        YR_ASSERT_MSG(false, "Node::addChild: would create a cycle");
        return false;
      }
    }

    raw->parent_ = this;
    children_.push_back(std::move(child));

    // 父已在树内：只让新加入的子树 enter，已有节点不重复 enter。
    if (tree_ != nullptr) {
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

    // 在 move/erase/清 parent_ 之前派发 exit，让回调看到完整的父子关系。
    Node* const raw = it->get();
    if (raw->tree_ != nullptr) {
      raw->setTree(nullptr);
    }

    // 先 move 出拥有关系再 erase，避免在 erase 后访问已释放的 unique_ptr。
    std::unique_ptr<Node> owned = std::move(*it);
    children_.erase(it);
    owned->parent_ = nullptr;
    return owned;
  }

  std::unique_ptr<Node> Node::detach() {
    Node* const oldParent = parent_;
    if (oldParent == nullptr) {
      return nullptr;
    }
    return oldParent->removeChild(this);
  }

  // 树内状态的唯一入口：切换 tree_ 前先对旧树做完整 exit（后序、子倒序）。
  void Node::setTree(SceneTree* tree) {
    if (tree == tree_) {
      return;
    }
    if (tree_ != nullptr) {
      propagateExitTree();
    }
    tree_ = tree;
    if (tree != nullptr) {
      propagateEnterTree();
    }
  }

  // guard 只包住 children 遍历：自己的 onEnterTree 中仍可 addChild 到 *this，
  // 但 child 的 enter 回调不能修改正在被遍历的 *this。
  void Node::propagateEnterTree() {
    YR_ASSERT_MSG(tree_ != nullptr, "Node::propagateEnterTree: node is not inside a tree");
    onEnterTree();
    const PropagationGuard guard(propagationDepth_);
    for (const std::unique_ptr<Node>& child : children_) {
      child->setTree(tree_);
    }
  }

  // guard 覆盖 children 倒序遍历与自身 onExitTree：退出期间 *this 结构被冻结，
  // this->addChild/removeChild 一律失败。
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

  // 即使 *this 已 ready 也不能提前返回：其下可能挂有离线新增、从未 ready 的后代，
  // 必须继续递归让它们补上 ready。先置位 everReady_，使子回调里动态挂入的 child
  // 判断父状态时一致。
  void Node::propagateReady() {
    YR_ASSERT_MSG(tree_ != nullptr, "Node::propagateReady: node is not inside a tree");
    if (tree_ == nullptr) {
      return;
    }

    const bool shouldNotify = !everReady_;
    everReady_ = true;

    // guard 只包住 children 遍历：child 的 ready 回调不能修改正在被遍历的
    // *this；guard 结束后自身 onReady() 中才可 addChild。
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

  void Node::onEnterTree() {}

  void Node::onReady() {}

  void Node::onUpdate(float /*deltaTime*/) {}

  void Node::onExitTree() {}

} // namespace yr::scene