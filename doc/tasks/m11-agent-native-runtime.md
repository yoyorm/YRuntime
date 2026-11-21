# M11 · AI 搭建并验证一个可玩场景

> 状态：阶段 A（全库架构走查中） → 阶段 B（最小可交付闭环）
> 最近更新：2026-11-21

## 1. 终点

让外部 AI **自己写出**一个简单场景的 `.scn`、**自己验证逻辑跑通**，然后人用带 Vulkan 后端的引擎**打开同一份 `.scn` 就能玩**。

样例固定为 **方块吃豆人**：移动、撞墙被挡住、碰到豆子加分并让豆子消失。

验收只有三个 yes/no：

1. AI 只读文本（不看截图、不点按钮）就能写出 `pacman.scn`；
2. headless 跑固定帧数后打出 `score=1, pellets=4`，重复跑结果一致；
3. `yr_render_demo --scene pacman.scn` 打开后 WASD 可控。

这一条闭环同时覆盖：玩家输入、碰撞阻挡、触发计数、物体销毁、渲染可见性。

## 2. 为什么是这条路

YRuntime 有四块原生基础，正好构成这条路的地基：

1. **Headless-first + 确定性步进**（`MainLoop::tick(fixedDelta)`）——AI 的自测因此可重复；
2. **反射与句柄安全网**（`ClassDB` / `PropertyInfo` / `Variant` / `ObjectID` generation）——能力可以被自描述；
3. **人和 AI 都能读写的文本场景**（`.scn`）——AI 的创作通道；
4. **纯数据的 `RenderSnapshot`**——不靠截图就能知道画面里有什么、缺什么。

M11 不做 GUI 编辑器、不引入脚本 VM、不把大模型放进 tick。
表达能力来自「反射属性 + 少量正交行为节点 + `.scn` 连线」，而不是新增语言。

**能力边界由原语决定**：给（计时、重叠、属性动作），AI 会拼出玩具场景；给（跳跃、重力、碰撞解算），那是另一个项目。

## 3. 阶段 A：架构走查（进行中）

走查时带着「AI 要如何读它、写它」的视角：

- **`object` 与 `serialize`**：`Variant` 有哪几种类型？`PropertyInfo` 登记了什么？`MeshInstanceNode` / `LightNode` / `CameraNode` 为何还没进 `ClassDB` 与 `.scn`？
- **`asset`**：资源从 `requestLoad` 到 worker 解码、主线程 `pump()` 安装、`collectGarbage()` 卸载的状态机；`.scn` 里的 `AssetUID` 由谁驱动加载。
- **`render_iface` 与 `vulkan`**：`RenderExtractSystem` 如何抽出 `RenderSnapshot`；只看快照能不能回答「这个物体为什么没显示」。

## 4. 阶段 B：四块积木（顺序即依赖）

> `.scn` 运行时闭环是整个链路的胶水：`[sub_resource]` 与 `[connection]` 目前只有解析与序列化，**没有实例化与接线代码**。它必须第一个做，否则会在最后卡住。

### 4.1 `.scn` 运行时闭环
- `MeshInstanceNode` / `LightNode` / `CameraNode` 注册进 `ClassDB`（属性表 + 工厂）；
- `[sub_resource]` 实例化（材质与颜色）、`[connection]` 接线（字符串信号 → 目标动作）；
- 仓库内置一个 `cube.glb`，所有方块引用它，颜色走 `Material.baseColorFactor`。

### 4.2 输入即状态
- GLFW 与 headless 注入写**同一个属性**，不允许两条输入路径；
- 真实键盘事件不可重放；属性化之后 AI 才能自测，过程才能复现。

### 4.3 网格化移动与阻挡
- 坐标吸附到整数格；「撞墙」= 查下一格是不是墙；
- 不做 AABB 穿透解算——那是物理引擎的入口。

### 4.4 触发 → 计分 → 销毁
- 新增两个最小节点：重叠检测（发信号）与属性动作（改目标属性）；
- `[connection]` 把重叠信号接到「计分 +1」与 `queueFree`；
- 复用 M4 延迟删除与 M7 快照 `releases`，豆子下一帧从画面消失。

## 5. 明确不做

- 实时回放、帧间 diff、`checkpoint` / `restore` 沙盒；
- 运行时挂起等待 AI 推理（模型不进入 tick，也不进入安全点）；
- 物理引擎、跨层性能归因、GUI 编辑器、脚本 VM；
- 为假设的消费者提前抽象：每个能力必须由 `pacman.scn` 真实使用。

## 6. 收口判据

- 一份 `pacman.scn`：headless 30 帧后 `score=1, pellets=4`，重复运行结果一致；
- 同一份文件用 `yr_render_demo --scene` 打开可玩；
- 换一个没进过这个仓库的模型，只靠 `schema` 导出与 2–3 份完整 `.scn` 样例也能跑完闭环；
- CI 门禁（build + ctest + format + `check_deps.py`）全绿。