# YRuntime — Agent Context

> 这个文件是**跨会话的上下文锚点**。新会话先读它，再读当前任务卡；聊天记录不是权威。
> 最近更新：2026-11-14

## 1. 项目一句话

YRuntime 是一个 C++20 游戏运行时：Object/SceneTree/EventBus/MainLoop + 资源系统 + Snapshot 驱动的 Vulkan 渲染后端。
当前已完成 M0–M9.5 主体；下一步是 **M10 任务系统**。

## 2. 当前任务

```text
M10-a：手写 JobSystem（MtQueue 已完成）
任务卡：doc/tasks/m10-a-job-system.md
用户负责：engine/job/include/yr/job/job_system.h、engine/job/src/job_system.cpp
AI 负责：tests/job/**、CMake、doc/**、机械式调用点迁移
```

MtQueue 已完成并通过 ASan/TSan；当前第一原则：**AI 不写 JobSystem 的实现**。
AI 只写测试、CMake、文档，运行并报告失败。

## 3. 架构不变量

- `Object` / `Resource` 的构造与注册只在主线程；worker 只能处理 POD 字节。
- 层依赖方向由 `tools/check_deps.py` 强制；不要新增反向 include。
- 只有 `engine/render/vulkan/` 可以 include Vulkan / GLFW。
- `yr_render_iface` 只依赖 core/std；快照是纯值 POD + vector，不持有 scene/asset 指针。
- Snapshot 当前为 **v3**（四贴图 + lights + shadow）；`doc/layers/render.md` 是字节格式权威。
- `.scn` 仍只保存 `position`；不扩展资源引用/灯光序列化；不做 UID 集中化。
- 决策记录在 `doc/06-decisions.md`（D28–D34 与 M9/M9.5 直接相关）。

## 4. 构建与验证

```bash
cmake --build --preset debug-vk
ctest --test-dir build/debug-vk

cmake --build --preset asan
ctest --preset asan

./tools/check_format.sh
python3 tools/check_deps.py -v
git diff --check
```

Vulkan demo 在当前环境需要 Xephyr（原生 `:0` 会卡 GLFW）：

```bash
Xephyr :99 -screen 1280x720x24 -ac &
DISPLAY=:99 ./build/debug-vk/bin/yr_render_demo --frames 60
```

## 5. 文件所有权（M10-a）

**用户手写，AI 不得改实现：**

- `engine/job/include/yr/job/job_system.h`
- `engine/job/src/job_system.cpp`

**AI 负责：**

- `tests/job/**`
- `engine/job/CMakeLists.txt`、`tests/job/CMakeLists.txt`、根 `CMakeLists.txt` / `tests/CMakeLists.txt` 的接线
- `doc/**`
- 机械式调用点迁移（改名、签名、include、格式化）

**机械替换规则：**

- AI 可以改用户文件，但仅限机械变更，不得改语义；
- 改之前说明范围，改完给 `git diff --stat` + 测试结果；
- 用户正在手写某个文件时，AI 不要并发编辑它；
- 机械变更单独 commit（`refactor(mechanical): ...`）。

## 6. 工作方式

1. AI 先写失败测试与 CMake 骨架（红灯）。
2. 用户实现核心文件。
3. AI 运行测试，只报告失败位置、断言、最小复现；不直接改实现。
4. 用户修复；AI 复查；小步 commit。
5. 需要设计解释时，AI 先查 `doc/06-decisions.md` / `doc/layers/**` / 代码，再回答。

**AI 不做：**

- 替用户写算法/数据结构；
- 在用户未要求时重构用户文件；
- 用“顺手修一下”的方式改变契约；
- 大规模生成核心实现后让用户 review。

## 7. 会话与上下文

- 同一个模块的问答留在同一会话；切换模块或上下文混乱时开新会话。
- 新会话开场提示：

```text
先读 AGENTS.md、doc/tasks/m10-a-mtqueue.md、doc/START-HERE.md、
doc/06-decisions.md 的 D34，以及 git log -5。
只回答问题和 review；不要改我负责的实现文件。
```

- 问过的设计结论回填到任务卡的 `Q&A / 决策记录`，不要只留在聊天里。
- 大范围机械修改前先 commit，避免和手写改动混在一起。

## 8. 提交风格

- subject-only 中文，例如：
  - `feat(job): 实现 MtQueue`
  - `test(job): 补充 MtQueue 并发用例`
  - `docs: 记录 M10-a 契约`
- 不写空泛的 “update code”；commit 是三个月后唯一的“为什么”记录。
