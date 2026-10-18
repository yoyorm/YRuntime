# scene 层（`yr_scene`）

> 代码位置：`engine/scene/include/yr/scene/` 与 `src/` · 运行时的骨架：`Node` 构成树，`SceneTree` 拥有根并驱动生命周期、更新与 Transform 刷新。
>
> 设计意图见 `../01-architecture.md` §4.6（Node-1 所有权决策、遍历策略、延迟删除）。本文件是**代码级契约的主来源**。

## Node（`scene::Node`）

- **职责**：组合出父子层级，并向子树传播 enter / ready / exit 生命周期与每帧 `onUpdate`。
- **所有权 / 生命周期**：
  - `children_`（`std::vector<std::unique_ptr<Node>>`）**独占**拥有子节点。
  - `parent_` 是**非拥有**裸指针；无父（树根或已摘下）时为 `nullptr`。
  - `addChild` **成功才转移所有权**，失败**不消费**参数、所有权仍归调用者。
  - `addChild` 后父若已在树内，会只让新子树 enter；父若已 ready，还会让新子树 ready。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `Node* parent() const` | 非拥有；无父返回 `nullptr` | — |
  | `SceneTree* tree() const` / `bool isInsideTree() const` | 所属树 / 是否在树内 | — |
  | `StringId name() const` | 名字（当前只读） | — |
  | `size_t childCount() const` | 子节点数 | — |
  | `Node* childAt(size_t) const` | 按下标取子 | 越界 = Debug assert + `nullptr` |
  | `bool addChild(std::unique_ptr<Node>&&)` | 成功即转移所有权并入树 | 见"不变量"四种拒绝 |
  | `std::unique_ptr<Node> removeChild(Node*)` | 摘除并交还所有权；**返回值必须接住，丢掉会立即销毁该子树** | 失败返回 `nullptr`（不改变树结构） |
  | `std::unique_ptr<Node> detach()` | 从父摘除自身，返回该子树所有权；无父返回 `nullptr` | — |
  | `bool isQueuedForDeletion() const` | 是否已入删除队列 | — |
  | `void queueFree()` | 延迟删除：置 `queuedForDeletion_` 并把 `ObjectID` 交给 `SceneTree` | 不在树内 = Debug assert + 安全返回；重复调用幂等 |
  | `const math::Vec3& position() const` | 父坐标系下的本地平移 | — |
  | `void setPosition(const math::Vec3&)` | O(1) 只改本地并置脏，不传播、不触发回调 | — |
  | `math::Mat4 globalTransform() const` | **上次 `refreshTransforms()` 的缓存**（读取契约见下） | — |

- **不变量 / 顺序**：
  - `addChild` 在**任何状态修改之前**做四项检查：本节点 `propagationDepth_ == 0`、`child != nullptr`、`child != this`、`child` 无父且 `child` 不是本节点的祖先（防环）。任一失败：assert + 返回 `false`，**不消费参数**。
  - 生命周期：`enter` **前序**（父先于子）；`ready` **后序**且**一生一次**；`exit` **后序、子节点倒序**。
  - **`propagationDepth_` 冻结语义**：`> 0` 时**本节点**的 `addChild` / `removeChild` 被拒（`detach()` 实际转发到父节点的 `removeChild`，故受父节点计数约束），保证正在遍历的 `children_` 不被增删。用**计数而非 bool**，因为嵌套 guard（如 `addChild` 在 enter 传播内再 enter）不能提前解除外层冻结。由 RAII `PropagationGuard` 增减，异常展开也安全。
  - **`everReady_`**：`ready` 一生一次的标记，`detach` / `exit` **不重置**——重新入树只重新 `enter`，不会自动再 `ready`。`propagateReady` 即使已 ready 仍继续递归，让离线新增、从未 ready 的后代补上 ready；先置位 `everReady_` 再递归，使子回调里动态挂入的 child 看到的父状态一致。
  - **`queueFree` 与 `queuedForDeletion_`**：只置标记并入队，**不改结构**。非树内节点属编程错误（所有权在外部），断言后安全返回、不置标记、不入队；重复调用幂等。`removeChild` / `detach` 后标志复位。
  - **`removeChild` 复位**：先派发 exit 再解除 `parent_`（让回调期间 `parent()` / `isInsideTree()` 仍反映旧位置）；先 `move` 出所有权再 `erase`，交还后清 `parent_` 并复位 `queuedForDeletion_`。
  - **Transform**：
    - `position_` 是**唯一可写数据源**，离线同样可写；不存 local 矩阵（refresh 时按需合成），避免 position 与 local 双源。
    - **槽生命周期**：首次入树分配（`acquireTransform`）、`detach` 保留、`Node` 析构释放（经 `transformOwner_` 归还）。
    - **重入树标脏**：同树重新入树也要置脏（`acquireTransform` 无条件 `localDirty = true`），否则旧 global 不是新父链下的正确值；跨树挂入先归还旧树槽再在新树分配。
    - **`globalTransform` 读取契约**：返回缓存值——树内节点为上次 refresh 的值；出树节点保留离开时的值；从未入树返回 identity。仅平移。
- **设计原因**（迁移自注释）：
  - guard 只包住 `children_` 遍历：节点自身的 `onEnterTree` / `onReady` 仍可 `addChild` 到 `*this`，但 child 的回调不能修改正在被遍历的本节点的 children。
  - `exit` 传播的 guard **覆盖** `children_` 倒序遍历与自身 `onExitTree`：退出期间本节点结构冻结。
  - `ready` 回调要求节点仍在树内（传播中被摘除时 assert）。
  - 三个回调契约：不得抛异常，不得修改正在传播的 parent。
- **Godot 对照**：`scene/main/node.h` / `node.cpp`（`_propagate_ready` / `_propagate_enter_tree` / `queue_free`）。
- **遗留 / 已知缺口**：
  - `NodePath`（`path()`）未实现；`name` 只读，名字唯一性未实现。
  - `requestReady`（显式再次 ready）未实现。
  - `onUpdate` 派发是**前序**，尚未提供 `setProcess` 开关与 priority 排序。
  - 第一版不搬 Godot 的 `owner`、internal child、groups、multiplayer、editor 逻辑。

## SceneTree（`scene::SceneTree`）

- **职责**：独占拥有 `root_`，驱动生命周期、每帧 `onUpdate`、Transform 刷新与延迟删除。三个安全点：`process` / `refreshTransforms` / `flushDeletions`。
- **所有权 / 生命周期**：
  - `root_`（`std::unique_ptr<Node>`）**独占**拥有根；子树由 `Node::children_` 级联拥有。
  - `setRoot` **成功才接管所有权**，失败不消费；`clearRoot` 与析构先 `exit` 再交还所有权。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `~SceneTree()` | 先让 root 子树 exit，再析构（在 transformStorage 仍存活时销毁 root，保证 `~Node` 能归还槽），不会重复 exit | — |
  | `bool setRoot(std::unique_ptr<Node>&&)` | 仅当 root 非空且当前无 root 时成功；先放入 `root_`（保证 enter 回调中 root 已被拥有且能拿回 `tree()`），再 `setTree(this)` → 全部 enter 完成后统一 `propagateReady` | 失败 = Debug assert + `false` |
  | `Node* root() const` | 空树返回 `nullptr` | — |
  | `std::unique_ptr<Node> clearRoot() noexcept` | 先 exit，再交还所有权并恢复空树；空树返回 `nullptr` | — |
  | `void process(float deltaTime)` | 前序派发 `onUpdate`；开始时一次性前序收集整棵子树的 `ObjectID` 快照，派发时经 `ObjectDB` 重解析 | 空树安全 no-op |
  | `void refreshTransforms()` | 前序 DFS 刷新整棵树的 global 缓存 | 空树安全 no-op |
  | `void flushDeletions()` | 执行本批延迟删除；开始时快照队列 | 空树安全 no-op |
  | `size_t transformCount() const` | TransformStore 槽数（含已 detach、尚未释放的节点），诊断用 | — |

- **三个安全点的职责与重入保护**：

  | 安全点 | 职责 | 重入 / 互斥保护 |
  |---|---|---|
  | `process` | `ObjectID` 快照前序派发 `onUpdate` | 检查 `processing_`（重入会让同一帧重复派发）、`refreshing_`；**不检查 `flushing_`**（见遗留） |
  | `refreshTransforms` | 前序 DFS 刷新 Transform 缓存 | 检查 `refreshing_`、`processing_`、`flushing_` |
  | `flushDeletions` | 执行延迟删除 | 检查 `flushing_`、`processing_`、`refreshing_` |

  - **process 的 `ObjectID` 快照重解析**：快照在开始时确定，回调中新挂入的节点本帧不 process；派发时逐个经 `globalObjectDB().get(id)` 重解析，解析失败（父先删使子 ID 失效）或已离树则跳过。因此**回调中删除节点是安全的**。
  - **refreshTransforms 前序 DFS + `parentRecomputed`**：`global = parentGlobal * T(position)`，仅当本地变过（`localDirty`）或**祖先本次被重算**时重算；`parentGlobal` 是父的缓存（root 用 identity）。子树被重算则整体重算，否则各自看 `localDirty`。
  - **flushDeletions 快照语义与三分支**：把 `pendingDeletions_` 换出到局部 batch，本批只处理它；flush 期间新 `queueFree` 写入已清空的队列，留给下一次 flush。每个 ID 经 `ObjectDB` 重解析后再分三支：
    1. 有父 → `detach()` 交出所有权，局部 `unique_ptr` 作用域结束即析构整棵子树；
    2. 是 `root_` → `clearRoot()` 先 exit 再交还所有权、析构；
    3. 无父且非 root 的孤儿（标志复位或又挂回树，所有权在外部）→ 断言 + 解除标记避免重复断言，不删除。
  - **标志各自防护对象与后果**（代码处只留一句概括，完整理由在此）：
    - `flushing_`：防止 `flushDeletions` 在自身执行中重入 —— exit 回调里若再 flush，会使外层 `removeChild` 持有的迭代器失效。
    - `processing_`：防止 `process` 重入导致同一帧重复派发；并让 process 期间的 `flushDeletions` 早退。
    - `refreshing_`：防止 `refreshTransforms` 重入；并让 process / flushDeletions 期间的 refresh 早退。
- **不变量 / 顺序**：
  - 三个安全点**互相排斥**，但 `process` 对 `flushing_` 的检查缺失（见遗留）。
  - **析构顺序**：成员声明顺序是 `transformStorage_` → `root_` → `pendingDeletions_`，按声明**逆序**析构，因此 `root_` **先于** `transformStorage_` 销毁——这正是为了让 `~Node` 还能用 `transformOwner_` 归还槽。
- **设计原因**：结构性修改收敛到安全点、用 `ObjectID` 快照 + 重解析代替"遍历中安全删除"，消灭迭代器失效类 bug（`01-architecture` §4.6）。
- **Godot 对照**：`scene/main/scene_tree.h`（`SceneTreeTimer` 在约第 57 行）、`scene/main/node.cpp` 的 `queue_free`、`scene/main/scene_tree_fti.cpp`。
- **遗留 / 已知缺口**：
  - **`process` 未检查 `flushing_`**：`refreshTransforms` 与 `flushDeletions` 都检查了三个标志，唯独 `process` 缺 `flushing_` 检查，构成三安全点互斥的唯一缺口。尚未决定是否补（补属行为改动）。
  - `refreshTransforms` **仅平移**：不支持旋转 / 缩放。
  - `NodePath` / `requestReady` 未实现（见 Node）。
  - 无 `physicsProcess` 相位、无 `SceneTreeTimer`。

## TransformData（`scene::TransformData`）

- **职责**：Transform 的最小状态（仅平移）——派生缓存 + 脏标记。
- **所有权 / 生命周期**：由 `TransformStore` 的 `SlotMap` 拥有；`Node` 只持有 `Handle`。
- **字段 / 公开 API**：

  | 字段 | 语义 / 契约 |
  |---|---|
  | `math::Mat4 global` | 上次 refresh 得到的 global 缓存 |
  | `bool localDirty` | 本地是否变化、需要重算；默认 `true` |

- **不变量 / 顺序**：不存 local 矩阵——`position` 是唯一可写数据源（在 `Node` 中），refresh 时按需合成 `parentGlobal * T(position)`，避免 position 与 local 双源。
- **设计原因**：单一数据源 + 懒合成（见 Node Transform 不变量）。
- **遗留 / 已知缺口**：仅平移。

## TransformStore（`scene::TransformStore`）

- **职责**：`TransformData` 的 `SlotMap` 容器；槽分配 / 释放由 `Node` 驱动。
- **所有权 / 生命周期**：独占拥有所有 `TransformData` 槽。`Node` 只持有 `Handle`，经 `SceneTree` 转发访问；槽**首次入树分配、detach 保留、`Node` 析构释放**。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `Handle<TransformData> create()` | 插入新槽 | — |
  | `void destroy(const Handle<TransformData>&) noexcept` | 归还槽 | — |
  | `TransformData* get(const Handle<TransformData>&)` / `const` | 失效返回 `nullptr` | — |
  | `size_t size() const` | 槽数 | — |

- **不变量 / 顺序**：`SceneTree` 必须比持有其 handle 的 `Node` 活得更久（`transformOwner_` 契约）。
- **设计原因**：SoA 式集中存储为后续并行刷新留空间；`Node` 不直接暴露 `TransformStore`，只经 `SceneTree` 转发。
- **遗留 / 已知缺口**：并发刷新未实现（当前单线程前序 DFS）。