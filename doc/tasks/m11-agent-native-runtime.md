# M11 总纲：全库架构走查 → AI 友好（Agent-Native）运行时架构

> 状态：阶段 A（全库架构走查中） → 阶段 B（三大支柱架构探讨与实现）
> 最近更新：2026-11-20

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

## 3. 阶段 B：走查完成后的三大架构探讨着力点（供后续在主对话逐个击破）

等你走查完上述模块并反馈感受后，我们将按**“你主导架构思考 → 我们探讨方案 → 你写头文件与核心骨架 → AI 填充细节与测试”**的方式，依次推进这三个核心支柱：

### 着力点 1：完备的反射自描述（Schema Introspection）与 `.scn` 场景闭环
- **要解决的问题**：打通 M2（反射）、M8（`.scn`）与 M9（渲染/资源节点）之间的历史断层。
- **可探讨的架构问题**：
  - `MeshInstanceNode`、`LightNode`、`CameraNode` 的属性（含完整 TRS 旋转/缩放、灯光参数、资源 `AssetUID`/路径）如何优雅地注册进 `ClassDB`？
  - 怎么设计一套零冗余的 Schema 导出机制，让外部工具/AI 一条查询就能获知引擎内所有可实例化节点类及其属性契约？
  - `.scn` 如何利用这套反射自动支持完整 3D 场景的存读档，让 `render_demo` 能够直接从 `.scn` 启动出画？

### 着力点 2：帧安全点的命令/事务协议（激活 `MainLoop` 阶段 2 `flush_messages`）
- **要解决的问题**：外部 AI Agent 或自动化脚本如何在不破坏帧内不变量、不引发悬垂指针的前提下，安全地操控正在运行的引擎。
- **可探讨的架构问题**：
  - 为什么外部控制命令必须汇聚到 `MainLoop` 阶段 2（`flush_messages`）执行？
  - 命令集应该如何抽象？（创建节点、销毁节点、通过 `ClassDB` 通用修改/读取属性、触发资源加载、可控步进 `step(N)` 帧）。
  - 当 AI 发来非法指令（如不存在的 `ObjectID`、类型不匹配的属性、非法的父子挂载）时，如何保证“绝不崩溃/assert trap，而是返回结构化的失败原因与修复提示”？

### 着力点 3：语义级世界与渲染快照可观测性（Semantic Snapshot & Verification）
- **要解决的问题**：解决传统引擎接入 AI 时“只能靠截屏猜状态、反馈极慢且不准”的痛点。
- **可探讨的架构问题**：
  - 如何把一帧结束时的 `SceneTree`（层级、局部/世界变换）、`AssetDatabase`（pending/ready/failed 资源状态）和 `RenderSnapshot`（实际提交给渲染器的 DrawItem、相机视锥内可见性、包围盒投影、BackendStats）整合成一份对 AI 极度友好的**语义级观测快照**？
  - 外部接入层采用什么最简形式（如基于 stdin/stdout 的行式 JSON-RPC / MCP 协议，或批处理脚本会话）既能做到零重型第三方网络库依赖，又能让真实 AI Agent 直接连上来操控你的引擎？
