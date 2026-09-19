# START HERE · 现在从哪里开始

> 生成于 2026-09-19。**这是你每次不知道"接下来干什么"时唯一需要打开的文件。**
> 它不重复其他文档的内容，只回答三件事：**怎么卸下心理负担、碰到决策怎么办、这周具体做什么。**

---

## 0. 先卸下三个心理负担

| 你在担心的 | 实际情况 |
|---|---|
| "文档太多，我记不住" | **不需要记住。** 见 §1：常驻只有 3 篇，其余是"按需查阅的参考书"，不是"要背的教材" |
| "很多决策我判断不了做还是不做" | **不需要你现在判断。** 见 §2 决策协议：绝大多数决策有默认答案，剩下的遇到时问我，我给选项+代价，你只做"选一个" |
| "我不理解全貌，不敢开始" | **M0 只跟 4 个文件有关**（`CMakeLists.txt`、`CMakePresets.json`、`.gitignore`、`.github/workflows/ci.yml`），跟反射/序列化/渲染一点关系都没有。全貌是做完 M4 之后自然浮现的 |

还有一条：**迷茫是正常的，而且是这个项目设计好的**。`02-roadmap.md` 里每个里程碑都写了"常见坑"，
就是因为我预期你会踩。踩了不是失败，踩了并且记进 `notes/` 才是这个项目的产出。

---

## 1. 文档分层使用法（不要试图全读）

| 层级 | 文档 | 什么时候打开 |
|---|---|---|
| **常驻**（每次都看） | 本文 · `02-roadmap.md` 的**当前里程碑那一节** · `notes/weekly/本周.md` | 每次开工前 3 分钟 |
| **开工前查**（每个模块一次） | `01-architecture.md` 的**对应 §4.x 小节** · `04-godot-study.md` 的**对应主题行** | 写设计笔记时 |
| **需要时才读**（参考书） | `00-vision.md`（怀疑方向时）· `03-learning-map.md`（卡住/要补知识时）· `05-engineering.md`（要查规范时）· `06-decisions.md`（要做取舍时）· `07-portfolio.md`（里程碑收口时） | 有具体问题才翻 |

> **规则**：任何一次开工，你实际需要的阅读量 ≈ 30 行。如果你发现自己在一口气读 500 行文档，
> 停下来 —— 那是在用"准备"代替"动手"（`04-godot-study.md` §5 最后一条：读代替写是拖延）。

---

## 2. 决策协议（碰到再决定，且不用凭经验）

### 2.1 四条默认规则（按顺序套用，命中就停）

| # | 规则 | 例子 |
|---|---|---|
| **R1** | **文档里已有倾向的，直接照做，不重新讨论。** `01-architecture.md` §11 的 Q1~Q6、`06-decisions.md` 的 D1~D15 就是为此写的 | Q1 数学库 → 自研最小版（D4 已定），不用再想 |
| **R2** | **两个方案都行 → 选代码更少、概念更少的那个。** 学习项目的敌人是复杂度，不是"不够先进" | 异步加载：回调 vs `std::future` → 先回调 |
| **R3** | **不确定要不要做 → 现在不做，写 `// TODO(M?)` 记下来，等它真的痛了再做。** 痛的表现是：你为了绕过它写了丑代码，或它让 bug 反复出现 | COW 字符串（D5）：先不做，等 E6 实验数据说话 |
| **R4** | **只有"影响 ≥1 天工作量"或"改起来很贵"的选择，才值得停下来问我** | 换序列化格式 = 问我；某个函数叫什么名字 = 不要问 |

### 2.2 问我的格式（这样我能给你有用的答案，而不是长篇大论）

```
问题：<一句话，具体到"我在写 X 时遇到 Y">
我的倾向：<A 还是 B>
我的理由：<哪怕理由是错的也写，这比问题本身更有价值>
```
我会回你：**2~3 个选项 + 推荐哪个 + 每个的代价 + "什么情况下你会后悔选它"**。你只需要选一个。

### 2.3 决定之后（30 秒，不要跳过）

- 影响架构/模块边界的 → 追加一条 ADR 到 `06-decisions.md`（用文末模板，**5 行就够**）
- 只影响实现的 → 写进当前模块的 `notes/design/<模块>.md`
- 都不影响 → 不用记

> **为什么值得记**：3 个月后你会完全忘记自己为什么这么选，然后花两小时重新纠结一遍。
> 而且"被推翻的决策记录"是面试里最有说服力的素材（`07-portfolio.md` §2 证据④）。

---

## 3. 第一周：M0 逐日计划

**M0 的唯一目标**：让"改一行 → 构建 → 测试 → 看到结果"这个回路跑起来，并让 CI 替你守规矩。
**不含任何引擎逻辑。** 完整验收标准见 `02-roadmap.md` 的 M0 一节，下面是把它拆成 7 天。

> 每天的结构都是：**动作 → 完成的证据**。证据是关键 —— 没有证据就等于没做。
> 时长是"专注时长"，不含查资料发呆；超了 50% 就停下来告诉我卡在哪。

### Day 1 · 仓库成型（~1.5h）
| 动作 | |
|---|---|
| 1 | 决定目录布局：**建议直接用 `engine/`，删掉空的 `src/`**（理由见下方「Day 1 的唯一决策」） |
| 2 | 建目录骨架（空目录 + `.gitkeep`）：`engine/core/{include/yr/core,src}`、`tests/core`、`cmake/`、`apps/`、`tools/`、`benchmarks/`、`.github/workflows/` |
| 3 | 写 `.gitignore`：`build*/`、`out/`、`.cache/`、`*.yrpak`、`compile_commands.json`、`*.o`、`*.a` |
| 4 | 补全 `.clang-format`（按 `05-engineering.md` §12 给的 yaml 显式写出 7 项） |
| 5 | 分 **3 个 commit**（不是一个）：`chore: 仓库初始化与分层骨架` → `docs: 项目规划文档` → `docs(notes): 2026-W38 周记`。理由：从第一天就练"一个 commit 一件事"（`05-engineering.md` §10） |
| 6 | 建 `notes/weekly/2026-W38.md`（复制模板；周号用 `date +%V` 查，别手写），本周目标写 3 条 |

**完成的证据**：`git log --oneline` 有 3 条；目录结构与 `01-architecture.md` §3 一致（本机没装 `tree`，用 `find . -path ./.git -prune -o -print | sort`）。
**Day 1 最大的坑**：**git 不跟踪空目录** —— 你建的 6 个空目录在 `git status` 里是完全隐形的，commit 之后 clone 出来什么都没有。每个空目录要放一个 `.gitkeep`。

<details><summary><b>Day 1 的唯一决策：src/ 还是 engine/？</b>（点开看理由，看完就照做，别再想）</summary>

**建议 `engine/` + `apps/` + `tools/` + `tests/` 四分。** 三个理由：
1. 顶层目录名本身就在说话：看到 `apps/` 就知道那是可执行 Demo，看到 `engine/` 就知道那是库。单一 `src/` 需要打开才知道里面是什么。
2. 你会有 **11 个 CMake target**（`01-architecture.md` §2）。目录即 target，一一对应，新人（含未来的你）不用读 CMake 就能猜到结构。
3. Yo_Renderer 就是单一 `src/` + `file(GLOB_RECURSE)`，结果分层无法强制，长出了 V1/V2 反向依赖（`core` include `render`）。这是你自己项目里的前车之鉴（ADR D11）。

**代价**：几乎没有 —— 现在 `src/` 是空的，`rmdir src` 就完事。三个月后改就要动所有 include。
**什么时候会后悔**：如果你决定整个项目只做 1~2 个 target，那 `src/` 更简单。但你的目标不是这样。
</details>

### Day 2 · 最小可构建 + 第一个测试（~2h，**本周最难也最值钱的一天**）
| 动作 | |
|---|---|
| 1 | 顶层 `CMakeLists.txt`：`cmake_minimum_required(3.24)` / `project(YRuntime LANGUAGES CXX)` / `add_subdirectory(engine/core)` / `add_subdirectory(tests)` |
| 2 | `cmake/YrLibrary.cmake`：一个 `yr_add_library(NAME ns SOURCES ... DEPS ...)` 函数，内部统一 `cxx_std_20`、警告 flags、`target_include_directories(PUBLIC include)` |
| 3 | `engine/core/CMakeLists.txt`：`yr_add_library(NAME yr_core NS yr::core SOURCES src/log.cpp)`（先放一个几乎空的 `.cpp`，让 target 存在） |
| 4 | `tests/CMakeLists.txt` + `tests/core/CMakeLists.txt`：`find_package(Catch2 3 REQUIRED)` → `YrTests_core` 链接 `yr::core` 与 `Catch2::Catch2WithMain` → `catch_discover_tests` |
| 5 | `tests/core/test_smoke.cpp`：一个 `TEST_CASE("smoke") { REQUIRE(1 + 1 == 2); }` |
| 6 | `enable_testing()`，跑通：`cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build/debug && ctest --test-dir build/debug --output-on-failure` |

**完成的证据**：ctest 输出 `100% tests passed, 0 tests failed out of 1`。
**这一天你会卡在哪**（提前告诉你，别慌）：`find_package(Catch2)` 找不到 / `catch_discover_tests` 未定义（要 `include(Catch)`）/ `PUBLIC` vs `PRIVATE` 传错导致 include 不到。
**卡住超过 30 分钟**：先查 [official CMake tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/index.html) 的第 1~3 步，再把具体报错原文发我。

### Day 3 · presets + sanitizer（~1h）
| 动作 | |
|---|---|
| 1 | 装 ccache：`sudo apt install ccache`（`05-engineering.md` §1 实测本机没有） |
| 2 | 写 `CMakePresets.json`：`debug` / `release` / `asan` 三个 configure preset + 对应 build/test preset（`asan` = `-fsanitize=address,undefined -fno-sanitize-recover=all`） |
| 3 | 验证：`cmake --preset debug && cmake --build --preset debug && ctest --preset debug` |
| 4 | **故意制造一次 UB**（比如 `int* p = nullptr; *p = 1;` 放进测试），确认 asan preset 会抓到并失败，然后删掉 |

**完成的证据**：三条 preset 命令全绿 + 你**亲眼看到** asan 报出一个真错误（这一步不能省，否则你不知道门禁是真是假）。

### Day 4 · 依赖检查脚本（~1.5h）
| 动作 | |
|---|---|
| 1 | 写 `tools/check_deps.py`：扫 `engine/**/*.h|cpp` 的 `#include <yr/...>`，比对 `01-architecture.md` §2 的允许矩阵，违规退出码 1 |
| 2 | 同一脚本加禁用符号检查：非白名单目录出现裸 `\bnew\b` / `\bdelete\b` → 报错 |
| 3 | **故意违规一次**：在 `engine/core/` 里写 `#include <yr/scene/node.h>`，确认脚本报错；再写一个 `new`，确认报错。然后删掉 |

**完成的证据**：两次故意违规都被抓到（截图或输出存进 `notes/evidence/M0/`）。
**这一天是我的活**：Python 脚本属于 `05-engineering.md` §9.1 允许 AI 生成的"样板"。**你可以直接让我写这个脚本**，但你要读懂它并能改允许矩阵。

### Day 5 · CI（~1.5h）
| 动作 | |
|---|---|
| 1 | 建 GitHub 仓库（**public**，理由见 `07-portfolio.md` §8 最后一条），`git remote add origin` |
| 2 | 写 `.github/workflows/ci.yml`：matrix(gcc-13, clang-18) × (debug, release, asan) → configure/build/ctest；再加 `format` job（`clang-format-18 --dry-run -Werror`）与 `lint` job（跑 `check_deps.py`） |
| 3 | push，去 Actions 页面**看它失败**，修到绿 |
| 4 | README 顶部加 CI 徽章 |

**完成的证据**：GitHub Actions 页面一片绿 + README 徽章可见。
**注意**：CI 上 `find_package(Catch2)` 需要 `sudo apt install libcatch2-dev`（runner 不自带），这是最常见的第一次失败原因。

### Day 6 · 吸收 yo_lib 第一批（~2h）—— 第一次"重构既有代码"练习
| 动作 | |
|---|---|
| 1 | `yo_assert.h` → `engine/core/include/yr/core/assert.h`：宏改名 `YR_ASSERT*`，加 `__builtin_trap()`（gdb 能直接停在断言处），加 `YR_BREAKPOINT()` |
| 2 | `logger/` → `yr/core/log.h` + `src/log.cpp`：按 `05-engineering.md` §5 改造 —— 加 tag / 帧号 / 线程 ID / 运行期 level 过滤，接口改成 `YR_LOG_INFO("tag", "fmt", args...)` |
| 3 | **保留** yo_lib 里那个 `InlineBuffer` 的 SBO 优化（那是好设计，别丢） |
| 4 | 写测试：level 过滤生效、tag 过滤生效、多线程并发写不交错（10 个线程各写 100 行，检查输出行数与完整性） |
| 5 | commit：`feat(core): 吸收 yo_lib 的 assert 与 logger 并重构` |

**完成的证据**：`ctest` 里有 ≥4 个 logger/assert 相关用例通过；asan preset 干净。
**这是第一次真正体会"吸收而非复制"**：你会发现 yo_lib 的 logger 用了 `std::ostream` 流式接口，
改成 fmt 风格宏之后**临时对象和构造开销都变了** —— 把这个观察写进 `notes/design/log.md`。

### Day 7 · 收口（~1h）
| 动作 | |
|---|---|
| 1 | 对照 `02-roadmap.md` M0 的验收标准逐条自检，把 checkbox 打勾并写日期 |
| 2 | 把本周的证据（ctest 输出、故意违规的截图、CI 绿灯截图）放进 `notes/evidence/M0/` |
| 3 | 更新根 `README.md`：M0 那一行状态改成 ✅，填上真实的"构建与运行"三条命令 |
| 4 | 填完周记：实际投入时长、学到的三件事、遇到的坑 |
| 5 | `git tag v0.M0` |
| 6 | **写下 M1 的第一条动作**（不用做，只写），下周开工时直接开始 |

---

## 4. 第二周预告（现在不要做）

M1 = `Handle<T>` / `SlotMap` / `StringId` / `Clock` / 最小数学库 + benchmark E1。
**第一天动作**：复制设计笔记模板 → `notes/design/handle-slotmap.md`，填 §1~§7，
其中 §7 要求你先读 `GodotDev/godot/core/templates/rid_owner.h`（40 分钟，时间盒）。
细节等 Day 1 做完再看 `02-roadmap.md` 的 M1 一节。

---

## 5. 每周固定节奏（周日晚 30 分钟 + 平日）

```
周日 30min：
  1. 复制周记模板 → notes/weekly/YYYY-Wxx.md
  2. 打开 02-roadmap.md 当前里程碑，挑 ≤3 个 checkbox 抄进周记
  3. 回看上周未完成项：是技术卡点还是估算错误？（这决定本周要不要减量）

平日每次开工前 3min：
  1. 看周记里今天的 checkbox
  2. 若是新模块 → 先开设计笔记（05-engineering.md §9.2 的 10 步流程）
  3. git status 确认工作区干净

每次收工前 5min：
  1. commit（一个 commit 一件事）
  2. 在周记里写一行"今天做了什么 + 卡在哪"
```

**允许"维持周"**（课业忙时只读 Godot + 写笔记，不写代码），但不允许连续两周（`02-roadmap.md` 节奏建议）。

---

## 6. 现在，接下来的 30 分钟做这三件事

- [ ] 读完本文 §1 和 §2（你已经在这了）
- [ ] 读 `02-roadmap.md` 的 **M0 一节**（约 40 行，15 分钟）
- [ ] 做 Day 1 的第 1~2 步（定布局 + 建空目录），然后 `git commit`

做完这三件事，你就不再"迷茫从何开始"了 —— 因为你已经开始了。

---

## 7. 分工：我可以做什么 / 我不做什么

依据 `05-engineering.md` §9.1 的铁规则。

### ✅ 你可以直接让我做（属于样板/教学，不影响"这是你独立做的作品"）
- `CMakeLists.txt` / `cmake/*.cmake` / `CMakePresets.json` 的样板
- `.github/workflows/ci.yml`、`tools/check_deps.py`、`tools/plot_stats.py` 等脚本
- `.clang-format` / `.clang-tidy` / `.gitignore` 配置
- 解释概念、讲清某个坑的原理、给 Godot 源码导读（带你读，而不是替你读）
- **review 你的设计笔记和头文件**（这是我最该被使用的地方：你写完 §1~§7 发我，我挑毛病）
- 给你的实现列测试用例清单（清单是规格，实现是你写）
- 你的代码报错时，帮你定位**错误类型**与排查方向

### ❌ 我不会做（除非你明确要求，且要求后我会提醒你这会削弱证据链）
- `engine/` 下任何模块的**实现代码**（Variant / ClassDB / SceneTree / 序列化器 / JobSystem…）
- 替你写 `notes/design/`、`notes/godot/` 的内容
- 替你写博客

---

## 8. 一句话总结

**不要理解全貌才动手。动手到 M4，全貌会自己浮现。**
现在去读 `02-roadmap.md` 的 M0，然后建目录、commit。
