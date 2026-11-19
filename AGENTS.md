# YRuntime — Agent Context

> 这个文件是**跨会话的上下文锚点**。新会话先读它，再读当前任务卡；聊天记录不是权威。
> 最近更新：2026-11-20

## 1. 项目一句话

YRuntime 是一个 C++20 游戏运行时：Object/SceneTree/EventBus/MainLoop + 资源系统 + Snapshot 驱动的 Vulkan 渲染后端。
当前已完成 M0–M9.5 主体、M10 与资产 worker 迁移；M9.5d-0 已实测；当前在 **M11 工具链与包装**（`yr_pack` 优先）。

## 2. 当前任务

```text
M11-1：yr_pack 打包工具（assets → .yrpak + 运行时 PakFileAccess）
计划：doc/02-roadmap.md M11 段；实测数据见 doc/m9-5d-0-sponza-benchmark.md
用户负责：review diff、确认格式与 CLI 取舍
AI 负责：tools/yr_pack、engine/asset 的 PakFileAccess、tests、CMake、doc
```

M10 / 资产 worker / Sponza 基准已完成；当前第一原则：**新格式先定契约再实现**，先在任务卡/ADR 写清
manifest + blob + 内容哈希 + 版本号，再动代码。

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

**用户手写（历史；M10 已收口，冻结）：**

- `engine/job/include/yr/job/job_system.h`
- `engine/job/src/job_system.cpp`

**AI 实现（分步提交，用户逐步 review）：**

- `engine/asset/src/loader_worker.*`
- `engine/asset/src/asset_database.cpp` 的 `pump` 安装段

**AI 负责：**

- `tests/job/**`、`tests/asset/**`
- `engine/asset/CMakeLists.txt`、`engine/job/CMakeLists.txt`、`tests/**/CMakeLists.txt` 的接线
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

**红灯规则：**

- AI 创建“等待用户实现”的失败测试或 stub 时，**不要提交**；
- 这些文件保留在工作区，等实现通过后与实现一起提交；
- 已提交的测试必须全绿，CI 不允许红灯；
- 如果已经误提交，先 `git reset --mixed HEAD~1` 收回，再让实现与测试一起提交。

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
