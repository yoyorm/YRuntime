# event 层（`yr_event`）

> 代码位置：`engine/event/include/yr/event/` 与 `src/event_bus.cpp` · 类型化同步事件总线（单线程）。
>
> 设计意图见 `../01-architecture.md` §4.5。`MessageQueue`（延迟调用）**尚未实现**。

## EventBus（`evt::EventBus`）

- **职责**：类型化同步事件总线，按事件类型分区，支持 `subscribe / unsubscribe / publish / post / flush`。
- **所有权 / 生命周期**：**仅主线程**。`subscribe` 返回的 `Subscription` 不拥有总线，但要求句柄不晚于总线销毁（见下）。不可拷贝 / 移动。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `Subscription subscribe<E>(std::function<void(const E&)>)` | 订阅类型 E；返回 RAII 句柄 | — |
  | `void unsubscribe(Subscription&) noexcept` | 标记 Entry `active = false`（**不立即物理删除**） | 句柄不属于本总线 / 已失效则 no-op |
  | `void publish<E>(const E&)` | **立即**同步派发，同一分表订阅者按注册顺序 | 递归深度超 `kMaxPublishDepth` → assert + 拒绝本次发布 |
  | `void post<E>(E&&)` | 入队到当前写缓冲，不立即派发；支持 move-only 事件 | — |
  | `void flush()` | 交换写 / 读缓冲，按 post 顺序（跨类型）派发上一帧入队的事件 | 递归 `flush` → assert + 早退 |
  | `size_t pending() const` | 当前写缓冲长度 | — |
  | `static constexpr size_t kMaxPublishDepth = 8` | 同步 publish 调用栈深度上限（总线级，不按类型分别计数） | — |
  | `globalEventBus()` | 进程级默认总线 | — |

- **不变量 / 顺序**：
  - **类型分区**：每种事件 `E` 一张独立分表（`type_index → deque<Entry>`）。
  - **用 deque 的原因**：`publish` 回调中订阅同类型会 `push_back`，deque 保证不会移动正在执行的 `std::function`。
  - **inactive 清理推迟到最外层**：`unsubscribe` 只置 `active = false`；`publishDepth_` 归零（栈上无任何 publish）时才 `cleanupInactive()` 物理删除，否则 `erase` 会使外层 publish 正在遍历的分表失效。保留空分表。
  - **深度上限 8**：`publish` 期间再 publish 同一（或任意）事件允许，但同步调用栈深度上限 8；第 9 层视为编程错误，Debug assert 且**所有构建都拒绝**本次发布（`01-architecture` §4.5）。
  - **post / flush 双缓冲**：`writeQueueIs0_` 明确表示 post 当前写入哪个队列；`flush` 交换后只排空刚换出的队列，因此 **flush 期间 post 的新消息进下一帧队列**，保证一帧内事件集合确定、可复现。flush 先弹出再派发，异常也不会重复派发。
- **设计原因**：`lastMsg_` 式的"缓存最后一条消息"隐式全局状态被刻意删除；用 `Subscription` 强类型句柄替代裸 token（`01-architecture` §4.5）。
- **Godot 对照**：signal 机制在 `core/object/object.h`（`connect` / `emit_signal`）；延迟调用在 `core/object/message_queue.h`（双缓冲 + flush）。
- **遗留 / 已知缺口**：
  - `MessageQueue` 未实现（依赖尚未具备的方法绑定与 MainLoop 安全点）。
  - 工作线程发布 / MPSC 队列推迟到 M10；当前 `publish` / `post` 无主线程 assert。

## Subscription（`evt::Subscription`）

- **职责**：订阅句柄（RAII），析构或 `reset()` 时自动退订。
- **所有权 / 生命周期**：**非拥有**其 `EventBus`——句柄不得晚于 `EventBus` 销毁；避免全局 / 静态句柄，必要时退出前显式 `reset()`。不可拷贝、可移动。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `Subscription() noexcept` | 无效句柄 | — |
  | `~Subscription()` | 自动 `reset()` | — |
  | 移动构造 / 移动赋值 | 转移句柄，源置为无效 | — |
  | `void reset() noexcept` | 主动退订并清空句柄；幂等 | — |
  | `bool valid() const` | `eventBus_ != nullptr && id_ != 0` | — |

- **不变量 / 顺序**：移动后源句柄失效，防止重复退订；`reset()` 先 `unsubscribe` 再清空字段（`unsubscribe` 也会清空句柄）。
- **设计原因**：用强类型 RAII 句柄替代裸 token，避免"忘记退订 / 重复退订"（`01-architecture` §4.5 yo_lib 迁移项）。
- **Godot 对照**：`core/object/object.h` 的 `connect` / `disconnect`。
- **遗留 / 已知缺口**：无。