# M11 总纲：全库架构走查 → AI 友好（Agent-Native）运行时架构

> 状态：阶段 A（全库架构走查中） → 阶段 B（B-1 交互底座 → B-2 正交行为节点与双层决策 → B-3 因果诊断与沙盒）
> 最近更新：2026-11-21（D35）

## 1. 为什么确定这个方向

YRuntime 没有传统 GUI 编辑器，但具备四大原生基因：
1. **Headless-first + 可控确定性步进**（`MainLoop::tick(fixedDelta)`）；
2. **反射与句柄安全网**（`ObjectID` generation 校验 + `ClassDB` / `PropertyInfo` / `Variant`）；
3. **人类与 AI 均可读写的确定性文本场景格式**（`.scn`）；
4. **纯数据的 `RenderSnapshot` 契约**（无需依赖截图，就能从数据层观测画面里有什么、缺什么）。

因此，M11 不做琐碎的外围工程包装，也不做模仿商业引擎的 ImGui 面板，而是深入解决一个在工业界和求职中都极具辨识度的核心问题：
**“如何从运行时架构层设计一套对 AI Agent / 自动化工具原生友好的控制协议与语义可观测体系？”**

---

## 2. 阶段 A：全库代码走查路线图与重点关注的“架构断层”

你目前已完成 `engine` 与 `scene`（`SceneTree`/`Node`）层的走查。接下来在你的探查对话中继续走查其余层次时，建议带着以下**“为 Agent-Native 架构找切入点”**的视角去审视现有代码：

### 待走查 1：`object` 与 `serialize`（连带 `scene` 中的序列化与派生节点）
- **核心文件**：`engine/object/include/yr/object/{object.h,class_db.h,property_info.h,variant.h}`、`engine/serialize/**`、`engine/scene/src/{scene_reader.cpp,scene_writer.cpp,packed_scene.cpp}`、`engine/scene/include/yr/scene/{mesh_instance_node.h,light_node.h,camera_node.h}`。
- **走查时重点观察的断层**：
  1. `Variant` 目前支持哪几种类型？`ClassDB` 里的 `PropertyInfo` 目前登记了什么？
  2. 为什么 M9/M9.5 新增的 `MeshInstanceNode`（持 `AssetUID` 与局部缩放/旋转）、`LightNode`、`CameraNode` 目前还没有接入 `ClassDB` 属性表和 `.scn` 序列化？
  3. 如果外部 AI Agent 想询问“当前引擎有哪些 Node 子类？每个类有哪些可读写属性？”，现在的 `ClassDB` 缺什么查询或自描述能力？

### 待走查 2：`asset`（资源生命周期与异步流式管线）
- **核心文件**：`engine/asset/include/yr/asset/{asset_database.h,async_loader.h,resource.h,resource_types.h,gltf_loader.h}`、`engine/asset/src/job_load_backend.*`。
- **走查时重点观察的断层**：
  1. 一个资源从 `requestLoad` / `loadGltfScene` 到 worker 解码、再到主线程 `pump()` 安装、最后到 `collectGarbage()` 卸载，状态机是怎么流转的？
  2. 如果在 `.scn` 或外部 Agent 命令里引用了一个外部资源（`AssetUID` 或 `res://` 路径），`SceneTree` 里的节点和 `AssetDatabase` 之间目前是谁负责驱动加载的？（注意目前 `yr_scene` 不依赖 `yr_asset` 或只存 `AssetUID`？检查 `check_deps.py` 的层依赖关系！）

### 待走查 3：`render_iface` 与 `render_vulkan`（渲染契约与语义信息）
- **核心文件**：`engine/render/iface/include/yr/render/{snapshot.h,backend.h,snapshot_io.h}`、`engine/engine/include/yr/engine/render_extract.h`。
- **走查时重点观察的断层**：
  1. `RenderExtractSystem` 在阶段 10 是怎么把 `SceneTree` 抽出 `RenderSnapshot` 的？
  2. 如果 AI Agent 不看截图，只看每一帧提取出的 `RenderSnapshot` + `BackendStats` + `AssetStats`，能不能直接诊断出“为什么某个物体没出现在画面里”（例如：资源还在 `kLoading` / `missing`？节点变换算出来的世界坐标在相机背面或视锥外？灯光没生效？）？

---

## 3. 阶段 B：递进执行路线（三阶段，不可跳跃）

阶段 B 不是一个平面的“三大支柱”，而是**有严格前置关系的三层递进**：

| 阶段 | 主题 | 交付的核心能力 | 前置依赖 |
|---|---|---|---|
| **B-1** | AI 交互基础底座 | 能**读**（Schema + 语义快照）、能**写**（帧安全点命令） | 阶段 A 走查完成 |
| **B-2** | 正交参数化行为节点 + 双层决策 | 能**编排**（反射三元组 + 原语连线）、能**分层决策**（C++ 反射弧 + 外部 AI 皮层） | B-1 的读写接口稳定 |
| **B-3** | 高级 AI 命令生态 | 能**解释**（因果诊断）、能**实验**（确定性沙盒） | B-2 的行为图可被外部观测与控制 |

**为什么不能跳**：B-2 的每个行为节点参数都要靠 B-1 的 Schema 自描述才能被外部读到、靠 B-1 的命令协议才能被安全改；B-3 的诊断与沙盒又必须建立在 B-2 产生的结构化行为图与 B-1 的确定性步进之上。反过来先做 B-3，就只能对着不存在的消费者造协议。

推进方式沿用四步：**用户主导架构思考 → 双方探讨取舍 → 用户写头文件与核心骨架 → AI 填充细节与测试**（见 `AGENTS.md` §3）。

---

## 4. 阶段 B-1 · AI 交互基础底座

### 支柱 1：完备的反射自描述（Schema Introspection）与 `.scn` 场景闭环
- **要解决的问题**：打通 M2（反射）、M8（`.scn`）与 M9（渲染/资源节点）之间的历史断层。
- **可探讨的架构问题**：
  - `MeshInstanceNode`、`LightNode`、`CameraNode` 的属性（含完整 TRS 旋转/缩放、灯光参数、资源 `AssetUID`/路径）如何优雅地注册进 `ClassDB`？
  - 怎么设计一套零冗余的 Schema 导出机制，让外部工具/AI 一条查询就能获知引擎内所有可实例化节点类及其属性契约？
  - `.scn` 如何利用这套反射自动支持完整 3D 场景的存读档，让 `render_demo` 能够直接从 `.scn` 启动出画？

### 支柱 2：帧安全点的命令/事务协议（激活 `MainLoop` 阶段 2 `flush_messages`）
- **要解决的问题**：外部 AI Agent 或自动化脚本如何在不破坏帧内不变量、不引发悬垂指针的前提下，安全地操控正在运行的引擎。
- **可探讨的架构问题**：
  - 为什么外部控制命令必须汇聚到 `MainLoop` 阶段 2（`flush_messages`）执行？
  - 命令集应该如何抽象？（创建节点、销毁节点、通过 `ClassDB` 通用修改/读取属性、触发资源加载、可控步进 `step(N)` 帧）。
  - 当 AI 发来非法指令（如不存在的 `ObjectID`、类型不匹配的属性、非法的父子挂载）时，如何保证“绝不崩溃/assert trap，而是返回结构化的失败原因与修复提示”？

### 支柱 3：基础语义快照查询
- **要解决的问题**：解决传统引擎接入 AI 时“只能靠截屏猜状态、反馈极慢且不准”的痛点。
- **本阶段只做基础版**：能回答“世界现在有什么”，`RenderSnapshot` 的深度归因留给 B-3。
- **可探讨的架构问题**：
  - 如何把一帧结束时的 `SceneTree`（层级、局部/世界变换）、`AssetDatabase`（pending/ready/failed 资源状态）和 `RenderSnapshot`（实际提交给渲染器的 DrawItem、相机参数、BackendStats）整合成一份对 AI 极度友好的**语义级观测快照**？
  - 外部接入层采用什么最简形式（如基于 stdin/stdout 的行式 JSON-RPC / MCP 协议，或批处理脚本会话）既能做到零重型第三方网络库依赖，又能让真实 AI Agent 直接连上来操控你的引擎？

> **B-1 收口判据（初步）**：外部进程可在无 GUI 条件下：查询 Schema → 通过命令协议或 `.scn` 构建/修改含网格与灯光的 3D 场景 → 确定性步进 N 帧 → 读取语义快照验证世界状态。

---

## 5. 阶段 B-2 · 正交参数化行为节点 + “脊髓反射 / 大脑皮层”双层决策

这是 M11 的**辨识度核心**，也是与“普通引擎加个 MCP server”拉开差距的地方。

### 5.1 用 `ClassDB + Variant` 把“代码逻辑”降维成“反射数据”

**核心约束**：静态编译的 C++ 不能靠 `switch (node->classId())` 或为每个可交互对象硬编码回调来支持 AI 编排——那样每加一个可被操控的属性，都要改引擎源码并重新编译。

**统一抽象**：把所有“条件判断”和“属性修改”收敛到同一个反射三元组

```text
(TargetNode, PropertyName, VariantValue)
```

- `TargetNode`：`ObjectID`（或 `.scn` 里的 NodeRef），解析时走 `ObjectDB` generation 校验，天然防悬垂；
- `PropertyName`：`StringId`，在目标类的 `ClassInfo` 属性表里查 `PropertyInfo`；
- `VariantValue`：`Variant`，由 `PropertyInfo` 的 setter 适配器校验类型。

**两条推论（整个 B-2 的地基）**：
1. 任何注册进 `ClassDB` 的属性，**天然可被通用条件判断器**（`== / != / > / <`）比较——只要 `Variant` 定义同类型比较与数值提升规则；
2. 任何可写属性，**天然可被通用属性动作器**（`Set` 直接赋值 / `Add` 数值累加 / `Tween` 在时长内插值）操作。

于是引擎不需要知道“手电筒”“吊桥”“巡逻路线”是什么，只需要知道“有一组属性、一个目标值、一个时间预算”。**表达能力来自数据组合，而不是新增 C++ 类**。

### 5.2 正交三段式原语 + `.scn` `[connection]` 信号组合

**拒绝 $N \times M \times K$ 的预制体矩阵**：N 种触发 × M 种运动 × K 种响应，组合爆炸；每加一个变体都要新 C++ 类、新属性表、新序列化分支与新测试。AI 也无法在运行时自行组合。

**拆分为 3 类极简正交原语**，通过 M8b 已有的 `.scn` `[connection]` 与 `EventBus` 拼装：

| 类别 | 职责边界 | 节点 |
|---|---|---|
| **触发器 / 传感器（When）** | 只检测、不发世界变更；条件满足时发出信号 | `TimerNode`、`ProximityTriggerNode`、`PropertyWatcherNode` |
| **连续运动控制器（Continuous）** | 每帧 `onUpdate(dt)` 驱动自身变换；参数全部反射 | `OrbitNode`、`OscillatorNode`、`FollowNode` |
| **通用响应器 / 动作器（Then）** | 被信号驱动，通过 `ClassDB` 改写或插值目标属性 | `PropertyActionNode`、`PropertyTweenNode`、`SpawnerNode`、`DestroyerNode` |

**组合自由度来自“连线”，而不是类数量**：N + M + K 个节点加连线，可表达远超 N×M×K 个预制体的场景；而连线本身是可序列化、可 diff、可由 AI 生成的 `.scn` 文本。

**关键设计点**：
- `PropertyActionNode` / `PropertyTweenNode` 能改写**任意**目标节点的**任意**已注册属性，**包括其他行为节点的 `enabled`**——即“行为本身也是属性”，可用同一套机制开关子系统，不需要专门的 enable 依赖图。
- 职责严格正交：触发器从不写世界；运动控制器只写自己的 `Transform`；响应器只写目标属性。三段不重叠，才能安全任意连线。
- 目标统一用 `ObjectID`/NodeRef 解析，不持有裸指针；目标已销毁时动作退化为 no-op 并记录结构化诊断，而不是崩溃。
- `SpawnerNode` 实例化子场景（复用 M8 `PackedScene::instantiate`），`DestroyerNode` 走 `queueFree`（复用 M4 延迟删除），不绕过既有安全点。

### 5.3 “C++ 脊髓反射弧 + AI 大脑皮层”双层运行模型

| 层 | 频率 | 执行体 | 负责 |
|---|---|---|---|
| **高频层 · 脊髓反射弧** | 每帧 60FPS | C++ 行为节点（`onUpdate` / 条件求值） | 连续运动、平滑插值、高频距离/计时检测、确定性反射联动 |
| **低频/事件决策层 · 大脑皮层** | 稀疏、按事件 | 外部 AI Agent（通过 B-1 协议） | 推理、改场景结构、改节点参数、重连行为图 |

**高频层**：完全在引擎进程内闭合，**即使 AI 不在线，世界也照常按已连线的行为确定性运行**。这是可离线验证的基线，也是“反射弧”的意义——不需要意识参与的条件反射。

**低频层**：当关键触发器触发（或收到显式 `pause` / `step` 命令）时，运行时在 `MainLoop` **帧安全点**把事件与当前语义快照推送给外部 AI Agent；Agent 推理后下发命令，在阶段 2 `flush_messages` 统一校验执行。

由此让静态编译的 C++ 引擎获得**由外部 AI 实时改变内部运行分支**的能力：
- 引擎不引入脚本 VM、不做代码热重载；
- AI 永远只看到安全点的完整状态，看不到帧内半成品；
- 命令失败返回结构化原因与修复提示，而不是 assert。

**分层的纪律**：不是每帧都问 AI。脊髓层无法处理的新情况才“上抬到皮层”。若每个 tick 都调用大模型，延迟与不确定性会同时摧毁确定性和交互手感——这正是本设计要避免的退化。

> **B-2 收口判据（初步）**：同一份静态编译的引擎，只靠 `.scn` 连线即可拼出“定时开灯 + 靠近触发 + 绕轨运动 + 属性渐变”等组合；AI 在同一次运行中通过安全点命令重连线或改参数，世界行为随之改变，且不重启、不重编译。

---

## 6. 阶段 B-3 · 拓展高级 AI 命令生态

B-2 让引擎“可编排、可被 AI 改分支”；B-3 让它“可解释、可实验”。

### 6.1 跨层因果诊断（Diagnostic & Spatial Oracle）

- **输入**：Mesh local AABB + 世界矩阵 + 相机视锥 + `AssetDatabase` 状态 + `RenderSnapshot`。
- **回答“为什么某物体未显示”**：按层逐项排除并输出证据字段——节点不在树内 / 资源 `kLoading`|`failed`|`missing` / AABB 完全在视锥外 / 在相机背面 / 世界矩阵退化 / 被 alpha mask 丢弃 / 无有效光照。
- **回答“这一帧为什么慢”**：把 `FrameStats`（14 阶段耗时）、`JobStats`（提交/完成/队列深度）、`AssetStats`（驻留/命中/字节）关联到同一帧，做性能瓶颈归因。
- **输出形态**：结构化诊断条目（原因枚举 + 证据字段 + 建议动作），而不是自由文本；外部 AI 可直接据此决策，不需要再猜。

### 6.2 可回滚确定性实验沙盒（Time-Travel Sandbox）

- **前提**：`MainLoop::tick(fixedDelta)` 已确定性 + `.scn` 可完整序列化。
- **能力**：`checkpoint`（世界完整存档）/ `step(N)`（确定性快进）/ `diff`（两次运行的语义差异）/ `restore`（一键回滚）。
- **用途**：AI 做“假设 → 快进仿真 → 量化对比 → 一键回滚”的闭环实验，而不污染当前世界；这让“AI 调参”从试错变成可验证的对照实验。
- **边界**：建立在序列化 + tick 确定性之上，不承诺跨平台浮点一致（需显式注明）；不引入完整的记录-回放指令流机制（那是 Stretch 级别）。

> **B-3 收口判据（初步）**：给定一个“看不见的物体”，诊断命令能给出唯一或有序的原因链；给定一组参数候选，沙盒能在同一 checkpoint 上分别快进 N 帧并给出可比较的语义 diff，然后回滚到初始状态。

---

## 7. 边界：M11 明确不做什么

- **不做 GUI 编辑器**（ImGui 可编辑检视器留在 Stretch S8）；
- **不引入重型脚本 VM**（Lua/自研 bytecode 留在 Stretch S1；M11 的表达能力来自反射属性 + 正交原语 + 连线）；
- **不把大模型放进每个 tick**（决策层只在安全点稀疏介入）；
- **不做行为树/状态机的通用图灵完备脚本层**——只做正交原语组合，表达能力上限由已注册属性与连线决定；
- **不为假设的消费者提前抽象**（延续 D26：每个子系统必须有真实 demo 消费者）；
- **不改变既有层依赖与线程铁律**：`Object`/`Resource` 构造仍只在主线程，`yr_render_iface` 仍只依赖 core/std，快照仍是纯值 POD。