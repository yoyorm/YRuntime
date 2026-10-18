# YRuntime

一个以理解游戏引擎运行时为目的的 C++20 学习型项目：先跑通可 tick 的 headless
Runtime，再以 renderer-neutral 的渲染契约把游戏状态接到渲染后端，形成
`游戏状态 → Runtime → RenderSnapshot → Renderer → GPU` 的链路。

![status](https://img.shields.io/badge/status-M4%20complete-brightgreen)
![CI](https://github.com/yoyorm/YRuntime/actions/workflows/ci.yml/badge.svg)
![cxx](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B&logoColor=white)
![platform](https://img.shields.io/badge/platform-Linux-lightgrey)
![license](https://img.shields.io/badge/license-MIT-blue)

## 目标与原则

- **headless-first**：先在无窗口、无 GPU 的环境里把运行时核心跑通、测透，再接入渲染。
- **依赖只向下**：分层 CMake target 与 CI 检查物理约束 include 方向。
- **可测试优先**：核心逻辑不依赖 GPU / 窗口即可单测，`ctest` 全绿是每步的底线。
- **Godot 4.x 为参照系**：对照其源码验证设计判断，而不是复刻。

## 架构

| target | 职责 |
|---|---|
| `yr_core` | 句柄、容器、时间、日志、断言、字符串 |
| `yr_object` | Object / ObjectDB / Variant / ClassDB |
| `yr_event` | 单线程 EventBus |
| `yr_scene` | Node / SceneTree |
| `yr_engine` | Engine / MainLoop / FrameStats（`World`、`RenderExtract` 规划中） |
| `yr_render_iface` · `yr_render_null` · `yr_render_vulkan` | RenderSnapshot 契约、NullBackend、Vulkan 后端（均规划中，M5 起） |
| `apps/runtime_headless` | 正式 headless host（M4 可运行物）；`render_demo` 与 `tools/` 打包 / 查看工具规划中 |

依赖方向由构建系统强制，完整分层见 [`doc/01-architecture.md`](doc/01-architecture.md)。

## 里程碑

| 里程碑 | 状态 | 一句话 |
|---|---|---|
| M0 工程地基 | 完成 | CMake / CI / Catch2 / assert / log |
| M1 句柄、容器、时间、字符串 | 完成 | Handle / SlotMap / Timer / StringId / ObjectID |
| M2 反射与对象模型 | 完成 | Object / ObjectDB / Variant / ClassInfo / ClassDB |
| M3 单线程 EventBus | 完成 | subscribe / publish / post+flush、重入与确定性重放 |
| M4 场景树与主循环 | 完成 | Node / SceneTree / Transform / MainLoop / Clock / FrameStats + Engine + `yr_runtime_headless --frames N` 与 10000 帧集成测试 |
| M5 渲染契约与 NullBackend | 未开始 | RenderSnapshot + RendererBackend 及 dump/replay |
| M6 最小 Vulkan Bootstrap | 未开始 | 窗口 + 清屏 + 硬编码三角形 |
| M7 Snapshot 驱动画面 | 未开始 | Runtime 移动 Node → 画面变化 |
| M8+ 序列化 / 资源 / 任务 / 工具链 | 未开始 | 见路线图 |

进度同时维护在根 README 与 [`doc/02-roadmap.md`](doc/02-roadmap.md)。

## 构建与测试

```bash
cmake --preset debug          # 配置
cmake --build --preset debug  # 构建
ctest --preset debug          # 测试
```

另有 `release` / `asan` preset（提交前跑 asan）。产物在 `build/<preset>/bin/`；
给 clangd 用：`ln -sf build/debug/compile_commands.json .`。

环境：GCC 13+ 或 Clang 18+（C++20）、CMake 3.25+ / Ninja、Catch2 3
（`sudo apt install libcatch2-dev`）、Linux。Vulkan SDK 与 GLFW3 仅在
`YR_BUILD_VULKAN_BACKEND=ON`（M6 起）时需要。

## 文档

| 文档 | 内容 |
|---|---|
| [`doc/START-HERE.md`](doc/START-HERE.md) | 现在在哪、下一步做什么 |
| [`doc/00-vision.md`](doc/00-vision.md) | 定位、目标 / 非目标、成功判据 |
| [`doc/01-architecture.md`](doc/01-architecture.md) | 分层、模块职责、帧循环、线程模型 |
| [`doc/layers/`](doc/layers/) | 分层代码契约（core / math / object / event / scene / engine） |
| [`doc/02-roadmap.md`](doc/02-roadmap.md) | 任务清单与验收标准 |
| [`doc/README.md`](doc/README.md) | 全部文档索引 |

## 相关项目

- [Yo_Renderer](https://github.com/yoyorm/Yo_Renderer)：一个 Vulkan PBR 渲染器。本项目把它作为
  Vulkan 底层机制的参考和选择性吸收来源（`VulkanContext`、`Swapchain`、`SyncObjects`、
  `CommandRecorder` 等），最小 Vulkan 后端以重写为主；不整体引入其 `rhi/` + `render/`，
  也不作为 subdirectory。
- Godot Engine 4.x：学习参照系。

## 许可

[MIT](LICENSE)
