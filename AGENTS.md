# YRuntime — Agent Context

> 这个文件是**跨会话的上下文锚点**。新会话先读它，再读当前任务卡；聊天记录不是权威。
> 最近更新：2026-11-20

## 1. 项目一句话

YRuntime 是一个 C++20 游戏运行时：Object/SceneTree/EventBus/MainLoop + 资源系统 + 任务系统 + Snapshot 驱动的 Vulkan 渲染后端。
当前已完成 M0–M10 全部核心模块；外围工程化内容已砍掉，当前聚焦最后一项核心功能：**M11-1 资源打包与 `PakFileAccess` 读取**。

## 2. 当前任务

```text
M11-1：资源打包与运行时 PakFileAccess 读取
任务卡：doc/tasks/m11-1-yr-pack.md
工作流：AI 指明推进方向 → 用户主导讨论架构 → 用户编写架构骨架（头文件/数据结构/核心接口）→ AI 填充实现细节、CLI/CMake 与测试
```

## 3. 核心工作方式（用户主导架构，AI 填充细节）

**严禁“AI 设计好架构/头文件模板，让用户填空写函数实现”**。正确顺序固定为四步：

1. **AI 只指明大致推进方向与问题边界**：
   - 说明当前模块要解决什么核心问题、依赖哪些既有接口、有哪些架构不变量；
   - **不要**预先替用户设计类结构、头文件签名、二进制字节布局或写好 `TODO(user)` 模板。
2. **用户主导讨论架构方案**：
   - 用户提出架构思路或问题，与 AI 探讨方案取舍；
   - AI 扮演讨论搭档（提供 Godot 等对照做法、指出生命周期/线程/耦合隐患），由用户自己拍板架构设计。
3. **用户编写大部分架构代码**：
   - 用户亲手写出头文件、核心数据结构、类/接口定义与关键控制流骨架。
4. **AI 填充细节与周边代码**：
   - 在用户搭好的架构骨架之上，由 AI 填充具体实现细节、边界错误处理、CLI 参数、CMake 接线、单元测试与格式化；
   - 用户审阅 AI 填充的代码，按小步提交。

## 4. 架构不变量

- `Object` / `Resource` 的构造与注册只在主线程；worker 只能处理 POD 字节。
- 层依赖方向由 `tools/check_deps.py` 强制；不要新增反向 include。
- 只有 `engine/render/vulkan/` 可以 include Vulkan / GLFW。
- `yr_render_iface` 只依赖 core/std；快照是纯值 POD + vector，不持有 scene/asset 指针。
- Snapshot 当前为 **v3**（四贴图 + lights + shadow）；`doc/layers/render.md` 是字节格式权威。
- `.scn` 仍只保存 `position`；不扩展资源引用/灯光序列化；不做 UID 集中化。
- 决策记录在 `doc/06-decisions.md`（D28–D34 与 M9/M9.5 直接相关）。

## 5. 构建与验证

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

## 6. 会话与角色建议

如需分会话，只需按当前所处的阶段切分，不必搞繁琐交接仪式：

- **架构讨论 / 推进规划会话**：对照 `AGENTS.md`、`doc/START-HERE.md` 与当前任务卡，和用户探讨架构方案，把讨论定下来的架构结论记入任务卡；
- **代码填充 / 测试验证会话**：读取用户写好的头文件与架构骨架，按用户指令填充 `.cpp` 细节、补齐 CMake 与测试并跑通验证。

## 7. 提交风格

- subject-only 中文，例如：
  - `feat(asset): 实现 PakFileAccess`
  - `test(asset): 补充 pak 读取测试`
  - `docs: 更新 M11 范围与协作工作流`
- 已提交的代码必须编译通过且 `ctest` 全绿；不写空泛的 “update code”。
