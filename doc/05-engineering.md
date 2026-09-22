# 05 · 工程规范与工作流

> 生成于 2026-09-19。本文在 M0 落地，之后当 checklist 用。
> **§1 的工具链现状是在本机实测的**（2026-09-19），不是通用模板。

---

## 1. 本机工具链现状（实测 2026-09-19，2026-09-22 修订）

> **本机是 Linux Mint 22.3 "Zena"**（`/etc/os-release`：`VERSION_CODENAME=zena`、`UBUNTU_CODENAME=noble`），
> 基座是 Ubuntu 24.04。规划期误记为"Ubuntu 24.04"，已更正。
> **这个区别在 Day 4b 造成了一次真实的 CI 失败**：本机装了 `libcatch2-dev`，但 Ubuntu noble 的 apt 源里没有这个包，
> 所以 CI 的干净环境装不上 → 见下表 Catch2 行与 ADR D13 的复盘。

| 工具 | 状态 | 版本 / 路径 | 备注 |
|---|---|---|---|
| GCC | ✅ | 13.3.0 | 主力编译器，支持 C++20 |
| Clang | ✅ | 18.1.3 | CI 第二编译器；`clang -E` 看宏展开必备 |
| CMake | ✅ | 3.28.3 | 满足 preset/`FetchContent` 全部需求 |
| Ninja | ✅ | 1.11.1 | 默认 generator |
| Catch2 | ✅ **已接入** | 本机 3.7.1（`/usr/lib/cmake/Catch2/`、`/usr/lib/libCatch2{,Main}.a`）。⚠️ **apt 源里没有**：`apt-cache policy libcatch2-dev` 只显示 `/var/lib/dpkg/status`，无任何仓库来源 | `tests/CMakeLists.txt` 用 `find_package(... QUIET)` + **FetchContent 回退**（v3.7.1，`GIT_SHALLOW`）。本机走系统包（configure 0.1s），CI 走 FetchContent（慢约 1~2 分钟，4c 用缓存缓解） |
| GDB | ✅ | 15.1 | 主力调试器 |
| LLDB | ✅ | 有 | 备用 |
| perf | ✅ | 有 | CPU 采样与 cache 统计（`perf stat` / `perf record`） |
| clang-format | ✅ | 18 | 与 Yo_Renderer 保持一致的版本 |
| ASan / UBSan | ✅ | GCC 实测可用 | `asan` preset |
| GLFW3 | ✅ | `/usr/include/GLFW` | M10 用 |
| Vulkan SDK | ✅ | `/home/yoyorm/SDK/Vulkan/1.4.357.1/x86_64`；另有 GLFW3（`/usr/include/GLFW`）与系统 `libvulkan.so.1.4.313` | **`VULKAN_SDK` 环境变量未设置**（2026-09-22 复查仍未设）。M10 前把 `export VULKAN_SDK=~/SDK/Vulkan/1.4.357.1/x86_64` 写进 `~/.bashrc`，并在 CMake 里做 fallback 查找 |
| Python | ✅ | 3.12.3 | `tools/check_deps.py` 等脚本用 |
| **clang-tidy** | ❌ 未装 | — | `sudo apt install clang-tidy` —— M0 或 M1 装 |
| **valgrind** | ❌ 未装 | — | `sudo apt install valgrind` —— 可选，ASan 已覆盖大部分场景；M6/M7 查内存时序问题时可能想要 |
| **ccache** | ❌ 未装 | — | `sudo apt install ccache` —— 安排在 **Day 5**（做 CI 缓存时才知道它省了多少）。CMake 侧用 `find_program` 探测，装了就用、没装不报错 |
| clangd | ✅ **已接入** | apt 安装 | 根目录 `compile_commands.json` → `build/debug/` 符号链接；`.vscode/settings.json` 里已禁用 C/C++ 扩展的 IntelliSense 避免冲突 |
| doxygen / graphviz | ❌ 未装 | — | 可选（M11 生成调用图/依赖图） |
| Tracy | ❌ 未装 | — | M11 可选，用 git submodule |
| gcovr / lcov | ❓ 待查 | — | M11 覆盖率用：`pip install gcovr`（需要 pip，当前 `python3-pip` 未检测到，`sudo apt install python3-pip gcovr`） |

**硬件**：12 线程 CPU / 31GB RAM / 磁盘剩余 240GB。
→ 对 M7 的意义：`JobSystem` 默认 `worker_count = 11`，benchmark 时记录这个值；
→ 对构建的意义：`-j12`。**实测 M0 规模：configure 0.15s / 全量 1.2s / 改一个 .cpp 增量 0.18s**（还没到需要 ccache 的量级）。

---

## 2. 构建系统规范

### 2.1 硬性规则
| 规则 | 理由 |
|---|---|
| **禁止 `file(GLOB_RECURSE)`**，源文件显式列出 | yo_lib / Yo_Renderer 都在用 GLOB；新增文件不触发重新配置是长期痛点。显式列表还能让"这个 target 有哪些文件"一目了然 |
| 每层一个 CMake target，`target_link_libraries` 只允许向下 | 依赖方向由链接器强制（原则 P1） |
| 头文件放 `include/yr/<layer>/`，`target_include_directories(... PUBLIC include)` | 外部一律 `#include <yr/core/handle.h>`，include 语句本身暴露分层 |
| 第三方依赖：系统包 > vendored header-only > FetchContent | Catch2 用系统包；nlohmann_json/GLM 可 vendor 到 `third_party/`（从 Yo_Renderer 拷）；FetchContent 只在必须时用（会拖慢冷启动且 CI 需要网络） |
| `cmake/YrLibrary.cmake` 封装样板 | 统一 std 版本、警告、include、导出；新增 target 一行搞定 |
| 每个 target 都要能被单独构建 | `cmake --build --preset debug --target YrTests_scene` |
| 输出目录统一 `build/<preset>/bin` | 与 Yo_Renderer 的习惯一致 |

### 2.2 CMakePresets（M0 建）
| preset | 用途 | 关键 flag |
|---|---|---|
| `debug` | 日常开发 | `-O0 -g3 -fno-omit-frame-pointer`，`YR_ENABLE_ASSERTS=ON` |
| `release` | 性能测试 / 录屏 | `-O3 -DNDEBUG`，断言保留但降级为日志（见 §6） |
| `asan` | 内存/UB 检查 | `-fsanitize=address,undefined -fno-sanitize-recover=all` |
| `tsan` | 数据竞争（M7 起） | `-fsanitize=thread`（**不能与 asan 同用**） |
| `ci` | GitHub Actions | `release` + `-Werror` + ccache |

每个 preset 都要能用 `ctest --preset <name>` 跑测试。

### 2.3 编译选项基线
```
-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
-Wcast-align -Wunused -Woverloaded-virtual -Wconversion=off（先关，M11 再考虑开）
-fvisibility=hidden（为将来 shared lib 留路，对照 yo_lib 的 yo_export.h）
```
`-Werror` **只在 CI preset**：本地开 `-Werror` 会让人在无关警告上卡住，反而降低开发速度。

---

## 3. 测试规范

### 3.1 组织
- 每层一个测试 target：`YrTests_core` / `_object` / `_event` / `_serialize` / `_asset` / `_scene` / `_engine` / `_render`。
- 文件按被测类命名：`tests/scene/test_node_lifecycle.cpp`。**一个测试文件 ≤ 400 行**，超了就拆。
- 用 `Catch2::Catch2WithMain`，除非需要自定义 `main`（比如注册 core types）。
- 全部注册进 ctest，`ctest --output-on-failure` 必须能看到失败详情。

### 3.2 测试命名与粒度
```cpp
TEST_CASE("Node: queue_free during own kProcess is deferred to phase 9", "[scene][node][lifecycle]") {
    SECTION("freeing self does not invalidate the current traversal") { ... }
    SECTION("freeing parent also removes children from the pending list") { ... }
}
```
- 名字描述**行为与保证**，不是"test1"/"testNode"。
- 用 tag 分组，方便 `ctest -R` 或 `./YrTests_scene [lifecycle]`。
- 一个 `TEST_CASE` 一个行为，`SECTION` 覆盖变体。

### 3.3 什么必须测（最低标准）
| 模块 | 必测项 |
|---|---|
| Handle/SlotMap | 失效检测、**ABA/index 复用**、大量插删后正确性 |
| RefCounted/Ref | 计数归零即销毁、拷贝/移动语义、循环引用（WeakRef 能破） |
| ClassDB | 注册/实例化/未注册类报错/继承链属性/`freeze()` 后注册失败/SIOF |
| Variant | 每种类型 round-trip、类型混淆检测、Dict 保序 |
| EventBus | §4.5 那三条重入语义、Subscription RAII、**同输入两次运行日志逐字节相同** |
| Node/SceneTree | 生命周期顺序（前序/后序）、四种删除场景、遍历中新增、NodePath 解析 |
| MainLoop | 固定步长累加器（含抖动输入）、暂停/时间缩放、spiral-of-death 上限、阶段顺序（用探针记录实际顺序并断言） |
| 序列化 | **round-trip 逐字节一致**、手写文件加载、未知属性容忍、版本迁移、循环引用 |
| AssetDatabase | in-flight 合并、失败路径、GC 卸载、慢 IO 不卡帧（ThrottledFileAccess） |
| JobSystem | 结果与串行一致（含浮点）、优雅停机、`wait_all` 在 worker 调用要 assert、`--jobs 0` 全测试仍过 |
| RenderSnapshot | dump/replay round-trip、POD 检查（`static_assert(std::is_trivially_copyable_v<...>)`） |

### 3.4 三类测试的分工
| 类型 | 位置 | 跑在哪 | 目的 |
|---|---|---|---|
| 单元测试 | `tests/<layer>/` | 每次构建 + CI | 语义正确 |
| 集成测试 | `tests/integration/`（如"跑 10000 帧 headless"） | CI | 系统协同、泄漏、稳定性 |
| 玩法回归测试 | `apps/text_adventure` 的 headless 自动通关脚本 | CI | 证明 Runtime 真能承载游戏 |
| benchmark | `benchmarks/` | **手动**（不进 CI，太慢且噪声大） | 性能判断，产出报告 |

### 3.5 测试必须能脱离真实环境（原则 P5）
- 文件系统 → 注入 `IFileAccess`，测试用 `MemoryFileAccess`。
- 时间 → 注入 `Clock`，测试用可手动推进的 `FakeClock`（**这是测固定步长与 Timer 的关键，M4 就要有**）。
- 线程 → `JobSystem(0)` = 同步执行；`--jobs 0` 全测试必须过。
- 渲染 → `NullBackend`。
- 随机 → 显式种子（`--seed`），失败可复现。

---

## 4. Sanitizer 策略

| Sanitizer | 何时跑 | 备注 |
|---|---|---|
| ASan + UBSan | **每次提交前**（本地 asan preset 跑一遍全测试）+ CI | 主力。注意 `-fno-sanitize-recover=all` 让 UB 直接失败 |
| TSan | M3（`post_from_any_thread`）、M6（异步加载）、M7（全量）+ CI 单独 job | 不能与 ASan 同用；Catch2 + TSan 可能对静态初始化报 suppress 项，需要 `tsan.supp` |
| MSan | 不用 | 需要全量 instrumented 依赖，成本过高 |
| 泄漏检测 | ASan 自带 LeakSanitizer；额外用 `ObjectDB` 存活计数在测试结束时断言为 0 | **ObjectDB 断言比 LSan 更有价值**：它能告诉你泄漏的是哪个类 |

---

## 5. 日志规范

吸收 yo_lib `logger/` 后重构为：
```cpp
YR_LOG_DEBUG("scene", "node {} entered tree at depth {}", name, depth);   // tag + fmt 风格
YR_LOG_INFO / YR_LOG_WARN / YR_LOG_ERROR / YR_LOG_FATAL
```
| 要求 | 说明 |
|---|---|
| 编译期 level 剔除 | `YR_LOG_DEBUG` 在 release 下展开为空（避免格式化开销与参数求值） |
| 运行期 level 过滤 | `--log-level debug`、`--log-filter scene,asset` |
| 输出内容 | 时间戳 / 帧号 / 线程 ID / level / tag / 消息（帧号与线程 ID 是 yo_lib 现在缺的，调试多线程必备） |
| 线程安全 | 单 mutex + 行缓冲；`InlineBuffer`（yo_lib 已有的 SBO 优化）保留 |
| 输出目标 | stderr（默认）+ 可选文件（`--log-file`）；CI 里全量收集 |
| **禁止** | 用日志代替返回值上报错误；在热路径（每帧每节点）打 INFO 以上日志 |

**日志是本项目最重要的调试工具**（headless 项目没有画面可看）。M0 就要做好，不要拖。

---

## 6. 断言与错误处理

| 情况 | 手段 |
|---|---|
| 不变量被破坏（程序 bug） | `YR_ASSERT(cond)` / `YR_ASSERT_MSG(cond, "...")`：debug 下打印表达式+文件行号并 `abort`（便于 gdb 直接定位），release 下**保留检查但只打 ERROR 日志 + 返回**（可配置） |
| 可恢复的外部错误（文件不存在、格式错、类未注册） | 返回 `std::optional` / `Expected<T, Error>` / bool + 日志；**不用异常** |
| 启动期 / 工具中的错误 | 可用异常（`yr::FatalError`），顶层 catch 后打印并退出 |
| 只读契约（"这个 API 只能主线程调"） | `YR_ASSERT(on_main_thread())` |
| 编译期约束 | `static_assert` + concepts（比运行期检查便宜得多，优先用） |

**规则**：断言消息必须能让人**不看代码就知道错在哪**。`YR_ASSERT(p)` 是失败的断言；
`YR_ASSERT_MSG(p, "AssetDatabase::pump() must run on main thread (called from job '{}')", job_name)` 才是合格的。

---

## 7. 调试工作流

### 7.1 常用配方
| 场景 | 做法 |
|---|---|
| 崩溃定位 | `cmake --preset debug` 构建 → `gdb --args ./build/debug/bin/yr_app ...` → `run` → `bt full` |
| ASan 报告看不清 | `export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1` + `llvm-symbolizer` 在 PATH |
| 断言触发即停 | gdb 里 `catch signal SIGABRT`，或直接 `YR_ASSERT` 里 `__builtin_trap()` |
| Catch2 单测断点 | `./YrTests_scene "Node: queue_free..." -s`（`-s` 打印成功断言）；gdb 里 `catch throw` 无用（不用异常），用 `break catch2::...` 或直接在测试代码里 `YR_BREAKPOINT()` |
| 观察一帧 | 在 `MainLoop::tick()` 各阶段入口下条件断点 `frame == 42` |
| 内存布局 | gdb `ptype /o yr::obj::Variant`（看 padding）、`print sizeof(...)` |
| 谁改了这个值 | gdb `watch -l obj.member_`（硬件观察点，慢但准） |
| 死锁 | `gdb -p <pid>` → `thread apply all bt` |
| 参考 Godot 的真实调用栈 | 编译 Godot debug 版后 `gdb --args bin/godot.*`，在 `Main::iteration` 下断点（见 `04-godot-study.md` §1.3） |

### 7.2 必备辅助设施（M0~M4 内建）
- `YR_BREAKPOINT()` 宏（`__builtin_trap()` / `raise(SIGTRAP)`）。
- `Engine::dump_state()`：把 SceneTree 结构、ObjectDB 存活统计、AssetDatabase 驻留表打成文本（**headless 项目的"截图"**，M4 就要有）。
- `--frames N` / `--script file.txt` / `--seed S` / `--dump-state` 命令行开关：让任何一次运行都可复现。
- core dump：`ulimit -c unlimited` + `/proc/sys/kernel/core_pattern` 配置说明写进 README。

---

## 8. 性能分析工作流

| 目的 | 工具 | 命令 |
|---|---|---|
| 快速看热点 | `perf` | `perf record -g ./build/release/bin/yr_app --frames 1000 && perf report` |
| cache / 分支统计 | `perf stat` | `perf stat -e cache-misses,cache-references,instructions,branches ./...` |
| 帧内阶段耗时 | 内建 `FrameStats` | `--stats-csv out.csv` + Python 绘图（M4 起） |
| 微基准 | Catch2 `BENCHMARK` 或独立 `benchmarks/` | 结果写进 `benchmarks/README.md` |
| 连续剖析（可选） | Tracy（M11） | submodule + `YR_PROFILE_SCOPE("...")` 宏（关闭时零开销） |

**benchmark 纪律**（否则数据无意义）：
① release 构建；② 预热 ≥3 次；③ 至少 5 次取中位数并记录方差；④ 用 `benchmark::DoNotOptimize` 或输出结果防止被优化掉；
⑤ 报告里写清 CPU/编译器/flags/数据规模；⑥ **数据要能推翻自己的假设才有价值**，只记录"符合预期"的数据是浪费。

**结果记在哪**：benchmark 代码放 `benchmarks/`，数据表与结论放 `benchmarks/README.md`（不单独建文档）。

---

## 9. ★ 自己写代码的工作法（本项目最重要的一节）

Yo_Renderer 的 README 写着"该项目使用 AI 辅助开发：仅为个人学习记录"。
YRuntime 的目标是**能作为能力证明的作品**（`00-vision.md` §4.5、`07-portfolio.md`），
所以必须建立一条清晰的"这是我自己设计并实现的"证据链。

### 9.1 铁规则
1. **AI 不写 `engine/` 下的实现代码**（构建样板除外：CMake / CI / Python 脚本）。允许 AI 做的事：
   - 解释概念、指出你代码里的 bug 类型、review 设计草案、提供 Godot 对照路径、生成 CMake/CI/脚本样板、出测试用例清单。
   - **不允许**：让 AI 生成 `Variant`/`ClassDB`/`SceneTree`/序列化器的实现然后粘进去。
2. **先把设计说出口 → 先头文件 → 先测试 → 再实现。** 顺序颠倒 = 你在"试出来"而不是"设计出来"。
   "说出口"不需要落文件（ADR D17）：在对话里讲一遍、讲不通的地方就是没想清的地方。
3. **一个 commit 一件事。** 搬运与改逻辑永不混在同一个 commit（Yo_Renderer `modularization-plan.md` §1 的经验，继续用）。
4. **每个模块完成后做一次"脱稿讲解"**：讲清这个模块为什么存在、和谁协作、失败模式是什么。讲不出的回去补——可以在对话里讲给 AI 听，让它挑毛病。

### 9.2 每个模块的标准流程（照做即可）
```
1. 读 01-architecture.md 对应小节 + 04-godot-study.md 指定文件（≤60min）
2. 在对话里过一遍设计要点：为什么存在 / 职责边界 / 所有权 / 失败模式 / 线程约束 / Godot 怎么做
   —— 不落文件。说不清的就是还没想清的，回到第 1 步
3. 写头文件（只有声明 + 每个 public 方法一句"契约注释"：前置条件/后置条件/失败行为）
4. 写测试（此时全部 fail —— 这是规格）
5. 实现，直到测试全绿（debug preset）
6. asan preset 跑一遍 → release preset 跑一遍
7. 重构（命名、去重、简化）+ 再跑测试
8. 更新 02-roadmap.md checkbox；架构级取舍才写 06-decisions.md
9. commit：标题说"做了什么"，**正文说"为什么"**（这是本项目唯一的高频记录，见 START-HERE §6）
```
> 第 2 步是**关键**，也是最容易被跳过的一步。它的替代品不是"写笔记"，而是"说出来" ——
> 说给自己听或说给 AI 听都行，但必须说出口。说不出口 = 没想清。

### 9.3 卡住了怎么办（按顺序尝试，不要第一步就问 AI）
1. 把问题写成一句话（写不出来 = 还没定位问题）。
2. 最小复现：写一个 20 行的独立 `.cpp` 复现，与引擎无关。
3. `clang -E` 展开宏 / `gdb` 单步 / 加日志缩小范围。
4. 读 Godot 对应文件（`04-godot-study.md`）——大多数问题成熟引擎都遇到过。
5. 搜具体错误信息（编译器报错原文、VUID、断言文本）。
6. **此时**再向 AI 提问，且要求"只解释原理和给方向，不给完整实现"。
7. 排查过程**不需要记录**，除非：① 同一个坑踩了第二次 → 写进 `02-roadmap.md` 对应里程碑的"常见坑"；
   ② 它推翻了某个设计 → 写 ADR。反复出现的坑才是知识，一次性的坑不是。

---

## 10. Git 工作流

| 项 | 约定 |
|---|---|
| 分支 | 主干 `master`（或改 `main`）；功能分支 `feat/<m>-<slug>`、`fix/...`、`refactor/...`、`doc/...` |
| commit message | Conventional Commits：`feat(scene): add deferred deletion to SceneTree`；正文写**为什么**，不写做了什么（diff 已经说明） |
| commit 粒度 | 一个 commit = 一个可独立构建 + 测试通过的变更。**禁止**"周末一次性提交 3000 行" |
| tag | 每个里程碑收口打 `v0.M4` 之类 |
| `.gitignore` | `build*/`、`out/`、`.cache/`、`*.yrpak`、`compile_commands.json`（或反过来提交它，二选一并坚持）、`.vscode/` 部分 |
| 大文件 | 资源（贴图/模型）用 Git LFS 或**只放小体积示例资源**；`.yrpak` 与录屏不进 git（放 release 附件或 `doc/media/` 下的小 GIF） |
| 提交前自检 | ① `cmake --build --preset debug` ② `ctest --preset debug` ③ `ctest --preset asan` ④ `clang-format` 无 diff ⑤ `check_deps.py` 通过 |

**commit 历史本身就是作品的一部分**：一个能看出"设计→实现→重构→测试"节奏的历史，
比一个只有 20 个大 commit 的历史有说服力得多（`07-portfolio.md` §2）。

---

## 11. CI 规范（GitHub Actions）

`.github/workflows/ci.yml`：
| job | 内容 | 触发 |
|---|---|---|
| `build-test` | matrix: {gcc-13, clang-18} × {debug, release, asan} → configure/build/ctest | push + PR |
| `tsan` | 单独 job（M7 起启用） | push + PR |
| `format` | `clang-format-18 --dry-run -Werror` 全仓库 | push + PR |
| `lint` | `tools/check_deps.py`（反向依赖 + 禁用符号）+ clang-tidy（M1 起） | push + PR |
| `coverage` | gcovr → 上传 + 徽章（M11） | push to master |
| `demo` | 构建 `apps/text_adventure` 并跑 headless 自动通关（M8 起） | push + PR |

要求：**CI 必须能在无 GPU、无窗口、无网络的 runner 上全绿**（这就是 headless-first 的红利）。
Vulkan 后端 target 用 CMake option `YR_BUILD_VULKAN_BACKEND=OFF` 默认关闭，CI 里不构建。

---

## 12. 代码风格细则

**`.clang-format` 已落地**（`BasedOnStyle: LLVM` + 显式固定 `ColumnLimit: 120` / `Standard: c++20` /
`SortIncludes: CaseSensitive` / `NamespaceIndentation: All` / `PointerAlignment: Left` 等）。
`.vscode/settings.json` 用 `xaver.clang-format` + `formatOnSave`，并禁用了 C/C++ 扩展的 IntelliSense（改由 clangd 提供）。

需要知道的两个后果：
- LLVM 基准是 **2 空格缩进 + 命名空间内缩进**，与 yo_lib / Yo_Renderer 的 4 空格不同 → 吸收旧代码时会被整体重排，这是预期的。
- **clang-format 只管 C/C++**。`CMakeLists.txt`、`.json`、`.yml`、`.md` 都是格式化盲区（M0 就踩过：`target_link_libraries` 的缩进丢了没人管）。
  CMake 文件量大了以后再考虑 `cmake-format`（决策协议 R3：不痛就不做）。

CI 门禁用 `clang-format-18 --dry-run -Werror $(git ls-files '*.h' '*.cpp')`（Day 5 的 `format` job）。

补充约定（clang-format 管不到的）：
| 项 | 约定 |
|---|---|
| 头文件自包含 | 每个 `.h` 都能单独编译（CI 生成一个 TU include 所有头验证） |
| include 顺序 | 对应 `.h` → C 标准库 → C++ std → 第三方 → 本项目（各组之间空行） |
| 前向声明 | 优先前向声明而非 include（降低编译依赖，这是大型 C++ 项目的关键习惯） |
| PIMPL | 只在"头文件暴露重量级依赖"时用（如 Vulkan 后端）；不滥用（会牺牲性能与可读性） |
| 单文件行数 | `.h` ≤ 300 行，`.cpp` ≤ 600 行；超了就是该拆的信号 |
| 函数行数 | ≤ 60 行；超了先想"是不是职责多了"，再想"怎么拆" |
| 注释语言 | 中文可以（与既有项目一致），但 **public API 的契约注释用中英皆可、必须写清前置/后置条件** |
| TODO | 必须带负责人与里程碑：`// TODO(M5): 支持 SubResource 循环引用` |
| 禁止 | 裸 `new/delete`、`reinterpret_cast`（除序列化 POD 且有注释）、`using namespace` 在头文件、全局可变状态（白名单：`ClassDB`/`ObjectDB`/`Engine`，均有 ADR）、`std::endl`（用 `'\n'`，除非要 flush） |

---

## 13. 资源与 assets 目录规范

```
assets_src/        # 源资源（可编辑格式：.png/.gltf/.md 剧情原稿）——M11 引入
assets/            # 运行时资源（.yrscn/.yrres/.json/.yrdlg）——M5 起
build/<preset>/assets/   # 构建时拷贝（或 yr_pack 产物 .yrpak）
```
| 规则 | 理由 |
|---|---|
| 路径在代码里一律用 `res://` 前缀的虚拟路径，由 `AssetDatabase` 映射到真实位置 | 对照 Godot；让 pak/磁盘/内存三种来源可互换 |
| 资源文件**不进 git LFS 也不放二进制大文件**，示例资源控制在几 MB | 仓库轻量，clone 快（面试官会 clone） |
| 每个资源目录放一个 `README.md` 说明来源与授权 | 作品项目的合规性 |
| 构建时用 CMake 自定义 target 拷贝资源，**不手动拷** | 可复现 |

---

## 14. 文档规范

**原则：文档要有限、可读。不为新东西造新文档**（2026-09-22 定，见 `START-HERE.md` §6）。

| 文档 | 更新时机 | 检查项 |
|---|---|---|
| `doc/START-HERE.md` | 当前进度变化时 | §1 状态表、§2 下一步是最新的 |
| `doc/02-roadmap.md` | 每完成一项 | checkbox + 日期；**与实际做法不一致的条目要改写，不要硬勾** |
| `doc/06-decisions.md` | 架构级取舍 | 新 ADR 追加，不改旧的（被推翻的标 `已推翻`） |
| `doc/01-architecture.md` | 分层 / 契约变化时 | Mermaid 图与代码一致 |
| `doc/05-engineering.md` §1 | 装了新工具时 | 工具表现状与本机一致 |
| `README.md`（根） | 每里程碑收口 | 里程碑表、构建命令、截图/GIF 是最新的 |
| `benchmarks/README.md` | 每个实验（E1~E16） | 方法可复现 + 有结论 + 有"对设计的影响" |
| `doc/07-portfolio.md` | **冻结**，M8 后修订 | — |

**不产出**：周记、设计笔记、Godot 对照笔记、evidence 文件。
这些信息分别由 **commit message 正文**（高频理由）、**ADR**（架构取舍）、**代码注释**（离开代码就会失效的细节，如内存序、帧阶段顺序、快照字段单位）承载。

**根 README 的最小结构**（M0 建骨架，M8/M10 补内容）：
项目一句话 → 状态与里程碑进度表 → 架构图 → 两个 Demo 的 GIF → 构建与运行（三条命令）→
模块说明表 → 测试与 CI 徽章 → 文档索引 → 致谢/参考。
