# YRuntime — Agent Context

> 这个文件是**跨会话的上下文锚点**。新会话先读它，再读 `doc/START-HERE.md` 与当前任务卡；聊天记录不是权威。
> 最近更新：2026-11-21（D35）

## 1. 项目一句话

YRuntime 是一个 C++20 游戏运行时：Object/SceneTree/EventBus/MainLoop + 资源系统 + 任务系统 + Snapshot 驱动的 Vulkan 渲染后端。
当前已完成 M0–M10 全部基础模块；外围琐碎工程化已砍掉。
**下一步核心演进方向**：不做传统 GUI 编辑器，在完成全库架构复盘走查后，深入构建 **AI 友好（Agent-Native）的游戏运行时控制与语义观测架构**。

## 2. 当前阶段与任务

```text
阶段 A（当前正在进行）：全库架构走查与断层梳理（用户逐层 review 代码与注释）
  - 已走查：engine / scene（SceneTree、Node、MainLoop 等）
  - 待走查：object & serialize（反射/Variant/.scn） → asset（异步资源流） → render_iface & vulkan

阶段 B（走查完成后的核心方向）：M11 · AI 友好（Agent-Native）运行时架构（D35）
  - 总纲任务卡：doc/tasks/m11-agent-native-runtime.md
  - 三层递进，不可跳跃：
    B-1 交互基础底座（能读能写）
      1. 完备 ClassDB 自描述 Schema + .scn 3D 场景闭环（打通 M2 + M8 + M9 断层）
      2. 帧安全点命令/事务协议（激活 MainLoop 阶段 2 flush_messages）
      3. 基础语义快照查询（SceneTree + AssetStats + RenderSnapshot 摘要 + 确定性步进）
    B-2 正交参数化行为节点 + “C++ 脊髓反射 / AI 大脑皮层”双层决策（能编排、能分层决策）
      1. 反射三元组 (TargetNode, PropertyName, VariantValue)：逻辑降维成反射数据
      2. When / Continuous / Then 三类正交原语，靠 .scn [connection] 组合，拒绝 N×M×K 预制体
      3. 高频 C++ 反射弧（60FPS）+ 帧安全点外部 AI 皮层（稀疏介入）
    B-3 高级 AI 命令生态（能解释、能实验）
      1. 跨层因果诊断（为什么没显示 / 为什么慢）
      2. 可回滚确定性沙盒（checkpoint / step(N) / diff / restore）
  - 明确不做：GUI 编辑器、重型脚本 VM、每帧调用大模型
```

## 3. 核心工作方式（用户主导架构，AI 填充细节）

**严禁“AI 预先设计好架构/头文件模板，让用户填空写函数实现”**。正确顺序固定为四步：

1. **AI 只指明大致推进方向与问题边界**：
   - 说明当前模块要解决什么核心问题、依赖哪些既有接口、现有代码里有哪些架构断层或不变量；
   - **不要**预先替用户设计类结构、头文件签名、协议字段或写好 `TODO(user)` 模板。
2. **用户主导讨论架构方案**：
   - 用户提出架构思路或走查发现的问题，与 AI 探讨方案取舍；
   - AI 扮演讨论搭档（提供 Godot 等对照做法、指出生命周期/线程/耦合隐患），由用户自己拍板架构设计。
3. **用户编写大部分架构代码**：
   - 用户亲手写出头文件、核心数据结构、类/接口定义与关键控制流骨架。
4. **AI 填充细节与周边代码**：
   - 在用户搭好的架构骨架之上，由 AI 填充具体实现细节、边界错误处理、CLI/CMake 接线、单元测试与格式化；
   - 用户审阅 AI 填充的代码，按小步提交。

## 4. 多对话分工约定

- **主规划对话（宏观方向）**：负责整体演进方向、阶段拆解、架构痛点梳理与方案探讨；不直接改业务代码。
- **探查 / 走查对话（代码复盘）**：配合用户逐层阅读、解释、清理注释与小幅重构现有代码；发现的架构断层回馈给主规划对话。
- **实现 / 测试填充对话**：在用户写好架构骨架后，按用户指令填充实现细节、CMake 与测试。

## 5. 架构不变量

- `Object` / `Resource` 的构造与注册只在主线程；worker 只能处理 POD 字节。
- 层依赖方向由 `tools/check_deps.py` 强制；不要新增反向 include。
- 只有 `engine/render/vulkan/` 可以 include Vulkan / GLFW。
- `yr_render_iface` 只依赖 core/std；快照是纯值 POD + vector，不持有 scene/asset 指针。
- Snapshot 当前为 **v3**（四贴图 + lights + shadow）；`doc/layers/render.md` 是字节格式权威。
- 决策记录在 `doc/06-decisions.md`（D28–D34 与 M9/M9.5 直接相关；D35 为 M11 Agent-Native 架构）。

## 6. 构建与验证

```bash
cmake --build --preset debug-vk
ctest --test-dir build/debug-vk

cmake --build --preset asan
ctest --preset asan

cmake --build --preset tsan
./tools/ctest_tsan.sh

./tools/check_format.sh
python3 tools/check_deps.py -v
git diff --check
```

Vulkan demo 在当前环境需要 Xephyr（原生 `:0` 会卡 GLFW）：

```bash
Xephyr :99 -screen 1280x720x24 -ac &
DISPLAY=:99 ./build/debug-vk/bin/yr_render_demo --frames 60
```

## 7. 提交风格

- subject-only 中文，例如：
  - `docs: 确立 Agent-Native 运行时演进方向`
  - `refactor(scene): 梳理 SceneTree 注释与接口边界`
- 已提交的代码必须编译通过且 `ctest` 全绿；不写空泛的 “update code”。
