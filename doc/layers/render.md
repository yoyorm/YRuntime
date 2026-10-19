# render 层（`yr_render_iface` + `yr_render_null`）

> 代码位置：`engine/render/iface/include/yr/render/`（+ `src/snapshot_io.cpp`）与 `engine/render/null/include/yr/render/null_backend.h`（+ `src/null_backend.cpp`）。
> 契约层 `yr_render_iface` 只依赖 `yr_core` 与 std；Null 后端 `yr_render_null` 依赖 `yr_render_iface` + `yr_core`。设计意图见 `../01-architecture.md` §4.11，推进范围见 `../02-roadmap.md` M5。本文件是**代码级契约的主来源**。

## render_iface（`yr::render` 契约层）

### AssetHandle（`yr::render::AssetHandle`）

- **职责**：资源句柄的**强类型**包装。跨 Runtime→Renderer 边界只作不透明 key。
- **字段 / 公开 API**：

  | 字段 / 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `std::uint64_t value = 0` | 原始值；`0` = 无效 | — |
  | `bool isValid() const noexcept` | `value != 0` | — |
  | `operator==`（default） | 按 `value` 比较 | — |

- **不变量 / 顺序**：默认构造即**无效**；不得从裸整数**隐式**转换（`std::is_convertible_v<uint64_t, AssetHandle>` 为 `false`）。注意：C++20 允许聚合的带括号逐字段初始化，`is_constructible` 仍为 `true`，类型安全靠"无隐式转换 + 具名字段"。
- **设计原因**：M5 无正式资源系统，用临时内建 triangle / default material ID 撑起契约；后端只当 key，不得索引运行时容器。
- **遗留 / 已知缺口**：正式 `AssetUID` 在 M9 替换本类型。

### RenderObjectId / TransformPod / DrawItem / CameraPod / RenderSnapshot

- **职责**：`SceneTree → Backend` 的纯值快照（M5 冻结最小版）。**不含任何 `yr::scene::*` / `Object*` / `Vk*` 类型，只传 ID 与值。**
- **逐字段语义与单位 / 坐标系**：

  | 类型 / 字段 | 语义 / 契约 |
  |---|---|
  | `RenderObjectId::value` | 承载运行时 `ObjectID` 的值；后端只当**不透明 key**，不得用它索引任何运行时容器，也不得假定其位布局 |
  | `TransformPod::m[16]` | 布局同 `yr::math::Mat4`：**column-major（index = col*4 + row）、列向量 v' = M · v、右手 Y-up、深度范围 [0,1]**（Vulkan NDC）。默认构造为 **identity**（与 `Mat4` 一致），不留全零退化矩阵 |
  | `DrawItem::object` | 该 item 对应的对象身份 |
  | `DrawItem::world` | 该对象的世界矩阵（列主序，同 TransformPod 约定） |
  | `DrawItem::mesh` | 网格引用；M5 为内建 / 无效 |
  | `DrawItem::material` | 材质引用；M5 为内建 / 无效 |
  | `CameraPod::view` / `proj` | 视图 / 投影矩阵，各为一个 `TransformPod`（同一矩阵约定） |
  | `RenderSnapshot::frame_index` | 递增帧序号（M5 单缓冲，单调不保证由后端强制） |
  | `RenderSnapshot::camera` | M5 **单相机**，固定默认值 |
  | `RenderSnapshot::items` | 本帧待绘条目；M5 按前序遍历顺序即确定，无排序键 |

- **不变量 / 顺序**：元素类型 `AssetHandle` / `RenderObjectId` / `TransformPod` / `DrawItem` / `CameraPod` 均 `std::is_trivially_copyable_v`（编译期断言在头文件内）；`sizeof(TransformPod) == 64`。`RenderSnapshot` 含 `std::vector`，**刻意不做 POD 断言**。
- **设计原因**：数据式契约把逻辑世界与图形 API 解耦——后端不得回调 Runtime、不得持有场景指针（D7）；换后端不需改 Runtime。
- **遗留 / 已知缺口**：`uploads` / `releases`（M9）、`LightPod`（M7）、相机 `fov`/`near`/`far` 与多相机（M7）、`sort_key` 位段（M7）、双 / 三缓冲交换协议（M7）均为**规划**，见 `../02-roadmap.md` M5 的"明确不做"表。

### BackendConfig / BackendStats / RendererBackend

- **职责**：后端接口与其输入 / 统计类型。
- **逐个语义**：

  | 类型 / 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `BackendConfig{}` | M5 headless：无窗口 / 设备参数；字段等到 M6 有消费者再加 | — |
  | `BackendStats::items` | 本帧条目数 | — |
  | `BackendStats::draw_calls` | 本帧 draw call 数 | — |
  | `BackendStats::triangles` | 本帧三角形数 | — |
  | `BackendStats::snapshot_bytes` | 快照编码字节数 | — |
  | `bool initialize(const BackendConfig&)` | 初始化设备 / 资源；成功返回 `true` | 失败返回 `false` |
  | `void shutdown()` | 释放资源；幂等 | — |
  | `void present(const RenderSnapshot&)` | 消费快照；**不得回调 Runtime** | — |
  | `const char* name() const noexcept` | 后端名（稳定字面量） | — |
  | `BackendStats last_frame_stats() const noexcept` | 最近一次 `present` 的统计 | 未 `present` 时为零值 |

- **所有权 / 生命周期**：`RendererBackend` 是接口；`present` 只借入快照的 const 引用，不取得其所有权、不得保存指针。后端实现由工厂（如 `create_null_backend()`）返回 `std::unique_ptr` 交给装配层。
- **设计原因**：接口只进快照、不出命令，后端无法反向依赖 Runtime；`BackendStats` 为后端自主性提供可测的观测点。
- **Godot 对照**：`servers/rendering/rendering_server.h`（**命令式 Server**：Runtime 发命令、场景侧只持 RID——我们刻意不照搬，改为一次性 POD 快照）；`core/templates/rid.h`（不透明 ID 与本层 `AssetHandle` / `RenderObjectId` 同源）。

### encodeSnapshot / decodeSnapshot

- **职责**：快照的确定性二进制序列化，用于 dump / replay 与契约完备性验收。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `std::vector<uint8_t> encodeSnapshot(const RenderSnapshot&)` | 按固定字段序写**小端**字节 | — |
  | `bool decodeSnapshot(const uint8_t* data, size_t size, RenderSnapshot& out)` | 校验 magic / version / 长度后解码 | 非法返回 `false` 且**不修改 `out`** |

- **字节格式**（全部小端；float 经 `std::bit_cast` 到 `uint32_t` 写入，不依赖宿主端序）：

  ```text
  magic        6 字节 "YRSNAP"
  version      u32 = 1
  frame_index  u64
  camera.view  16 × f32
  camera.proj  16 × f32
  item_count   u64
  重复 item_count 次：
    object     u64
    world      16 × f32
    mesh       u64
    material   u64
  ```

- **不变量 / 顺序**：
  - **确定性判据**：固定字段序 + **不复用结构体内存布局**（逐字段写，天然绕开 padding / 对齐），因此同一输入两次编码**逐字节相同**。
  - **解码严格性**：magic 必须匹配、`version == 1`、长度必须**恰好**匹配（多一个字节也算非法）；任一不满足返回 `false` 且 `out` 保持不变（内部先解码到局部变量，全部通过才 `out = std::move(...)`）。
- **设计原因**：dump / replay 是"契约真的完备"的可执行证据；不 memcpy 结构体使格式与 ABI / 编译器布局无关。
- **遗留 / 已知缺口**：无 CLI（`--dump-snapshot` / `--replay-snapshot`）接入（推迟）；无版本迁移逻辑。

## NullBackend（`yr::render::NullBackend`）

- **职责**：`RendererBackend` 的 headless 实现，**只统计不画**，用于在没有 GPU 时驱动契约与确定性验收。
- **所有权 / 生命周期**：由 `create_null_backend()` 返回 `std::unique_ptr<RendererBackend>`；`initialize` 置 `initialized_ = true`，`shutdown` 置 `false`，二者均不拥有外部资源。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `bool initialize(const BackendConfig&) override` | 置 `initialized_ = true`，恒返回 `true` | — |
  | `void shutdown() override` | 置 `initialized_ = false`；幂等 | — |
  | `void present(const RenderSnapshot&) override` | 计算并存统计，不产生任何绘制 | — |
  | `const char* name() const noexcept override` | 恒为 `"null"` | — |
  | `BackendStats last_frame_stats() const noexcept override` | 最近一次 `present` 的统计 | — |
  | `std::unique_ptr<RendererBackend> create_null_backend()` | 构造 `NullBackend` 的工厂 | — |

- **统计口径**（`present` 时计算）：
  - `items = snapshot.items.size()`
  - `draw_calls = snapshot.items.size()`
  - `triangles = snapshot.items.size()`（M5 约定每个 item 一个内建三角形）
  - `snapshot_bytes = encodeSnapshot(snapshot).size()`
- **不变量 / 顺序**：同输入 `present` 两次统计完全相同（依赖 `encodeSnapshot` 的确定性）。`name()` 不依赖初始化状态。
- **设计原因**：NullBackend 是"契约消费者"的最小实现——它跑得通，说明快照自洽且可被独立后端消费，而无需任何图形 API。
- **Godot 对照**：`servers/display/display_server.h` 的 headless 形态（对照阅读）。
- **遗留 / 已知缺口**：`initialized_` 目前只作状态标记，`present` 不校验它；无依赖 `initialized_` 的行为。真实后端（`yr_render_vulkan`）在 M6/M7。

## 本层遗留 / 接线现状

- **快照的产出在 engine 层**：`yr_engine` 的 `RenderExtractSystem`（`engine/engine/include/yr/engine/render_extract.h`）是 `SceneTree → RenderSnapshot` 的唯一单向转换点（D7 规则②）；帧阶段 10 `extract_render` 调用它，阶段 11 `present` 把快照交给装配者注入的非拥有 `RendererBackend*`。本层（`yr_render_iface` / `yr_render_null`）只提供契约与 Null 后端，不含抽取逻辑；层契约细节见 `engine.md` 的 `RenderExtractSystem` 小节。
- 其余推迟项（uploads / releases / lights / sort_key / 多相机 / 双三缓冲 / 独立渲染线程）见 `../02-roadmap.md` M5 的"明确不做"表。