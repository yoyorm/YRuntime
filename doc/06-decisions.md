# 06 · 架构决策记录（ADR）

> 生成于 2026-09-19。
> **规则**：① 重要取舍必须在此留一条；② 推翻旧决策时**追加新 ADR** 并把旧的标为 `已推翻（被 Dxx 取代）`，
> 不要删除或就地改写——被推翻的决策记录本身就是最有价值的学习材料；③ 每条都要写"什么情况下我会改回来"。

## 状态图例
`暂定` = 规划期基于假设做出，可被现实推翻 · `已确认` = 实践验证过 · `已推翻` = 被后续 ADR 取代

## 索引

| ID | 标题 | 状态 | 里程碑 |
|---|---|---|---|
| D1 | yo_lib 被吸收进 YRuntime 而非作为外部依赖 | 暂定 | M0~M1 |
| D2 | 主体用 Object 树 + 反射，数据密集处用 SoA（双范式并存） | 暂定 | 全程 |
| D3 | 句柄 = index + generation（SlotMap），与全局 ObjectID 并存 | 暂定 | M1~M2 |
| D4 | 自研最小数学库，GLM 只在 Vulkan 后端用 | 暂定 | M4（原定 M1，2026-09-26 推迟） |
| D5 | Variant 精简实现（9 种类型），Dict 保序 | 暂定 | M2 |
| D6 | 反射注册宏限制在 3 个以内，先手写后宏化 | 暂定 | M2 |
| D7 | 渲染解耦 = POD 快照契约 + 单向抽取 + 零回调 | 暂定 | M9~M10 |
| D8 | 线程模型：逻辑单线程，工作线程只碰 POD 与不可变数据 | 暂定 | M6~M7 |
| D9 | 序列化格式自研 `.yrscn`（文本）+ `.yrscnb`（二进制），JSON 仅调试 | 暂定 | M5 |
| D10 | 场景结构修改收敛到帧阶段 9，其余阶段只改数据 | 暂定 | M4 |
| D11 | 分层 = 分层 CMake target，依赖方向由构建系统与 CI 强制 | 暂定 | M0 |
| D12 | Linux-first，平台层保留薄抽象但不移植 | 暂定 | 全程 |
| D13 | 测试用 Catch2（系统包优先 + FetchContent 回退），benchmark 独立且不进 CI | 暂定（09-22 复盘修订） | M0 |
| D14 | 不使用异常做控制流；错误经返回值 + 日志上报 | 暂定 | 全程 |
| D15 | Vulkan 后端由 Yo_Renderer 搬入并改名空间，而非 submodule 引用 | 暂定 | M10 |
| D16 | Sanitizer 与 Debug 构建分离，而不是合并 | 暂定 | M0 |
| D17 | 仓库内不产出笔记文件，记录只靠 commit message + ADR + checkbox | **已确认** | M0 |
| D18 | 检验标准是"能否讲清楚"，不是"能否自证"（删除全部证据链要求） | **已确认** | M0 |

---

## D1 · yo_lib 被吸收进 YRuntime 而非作为外部依赖
**状态**：暂定（前置假设 A2）· **里程碑**：M0~M1

**背景**：已有 `/home/yoyorm/Code/yo_lib`，含 logger / assert / sparse_set / eventsys / mtqueue / timer / matrix。
三种处理方式：吸收演进 / submodule 引用 / 完全重写。

**决策**：**吸收**。把需要的部分搬进 `engine/core`、`engine/event`、`engine/job`，搬入时逐个重构（统一命名、补测试、删隐式全局状态），
之后 yo_lib 不再演进（保留归档）。

**备选与否决理由**：
- *submodule 引用*：接口冻结成本高，任何改动要跨仓库协调；且 yo_lib 目前无测试、命名不统一（`m_` 与 `trailing_` 混用、`#pragma once` 与 include guard 混用），直接依赖会把技术债带进新项目。
- *完全重写*：浪费已有成果，且 logger 的 `InlineBuffer` SBO 优化是真做过思考的。

**后果**：+ 可自由重构、边界清晰、成为作品的一部分；− M0/M1 工作量增加约 1 周；− yo_lib 与 YRuntime 出现功能重复（明确以 YRuntime 为准）。

**什么情况下改回来**：如果 yo_lib 被其他项目（如 Yo_Renderer 后续）实际复用且需要共享演进，则改回独立仓库 + submodule。

**搬入清单与改造要求**：
| yo_lib 文件 | 去向 | 必做改造 |
|---|---|---|
| `yo_assert.h` | `yr/core/assert.h` | 宏改名 `YR_*`；加 `__builtin_trap` 便于 gdb；release 行为可配置 |
| `logger/*` | `yr/core/log.h/.cpp` | 加 tag / 帧号 / 线程 ID / 运行期 level 过滤 / 宏化 `YR_LOG_*`；保留 `InlineBuffer` SBO |
| `yo_sparse_set.h` | `yr/core/sparse_set.h` | `int` 哨兵 → `uint32_t npos`；加 stable/unstable 两种 erase；加迭代器；补测试 |
| `yo_eventsys.h` | `yr/event/event_bus.h` | 删 `lastMsg_` 隐式缓存；`uint16_t token` → RAII `Subscription`；`type_index` → 静态 `kTypeId`；补重入语义与测试 |
| `yo_mtqueue.h` | `yr/job/queue.h` | 加 `try_pop`；`stop()` 幂等；去掉 `push` 的无界等待（改有超时/失败返回） |
| `yo_timer.h` | `yr/core/time.h` | 拆成 `Clock`（引擎时间源）与 `ScopeTimer`（性能测量）两个东西 |
| `yo_matrix.h` | **不搬** | `std::vector` 存储的矩阵无性能价值；见 D4 |
| `yo_export.h` | `yr/core/export.h` | 保留（为将来 shared lib 留路） |

---

## D2 · 主体用 Object 树 + 反射，数据密集处用 SoA
**状态**：暂定 · **里程碑**：全程

**背景**：Yo_Renderer 用了 ECS（SparseSet 组件池），Godot/Unity 传统是 Object 树 + 组件组合。两种范式必须选一个为主。

**决策**：**Object 树 + 反射为主体**（`Node` 继承 `Object`，属性经 `ClassDB` 暴露），
在三处使用 SoA/紧凑数组：① Transform 世界矩阵平行数组（M4）② `RenderSnapshot::items`（M9）③ 若做 stretch S3，渲染实例数据整体入 SparseSet。
**边界规则**：逻辑身份用 Object，批量数据用 SoA；**Object 持有 index 指向 SoA 行，SoA 不持有 Object 指针**。

**理由**：项目目标是理解 Runtime 与工具链，而序列化（M5）、检视器（M2）、事件（M3）、场景层级（M4）、未来脚本绑定（S1）**全部依赖反射**；
纯 ECS 会让这些功能都要额外造一套元数据。同时你的 Yo_Renderer 已经练过 ECS，重复投入价值低——但"两种范式的边界"恰恰是最有面试价值的认知（见 `03-learning-map.md` §5）。

**备选与否决理由**：*纯 ECS*（EnTT 风格）：并行化更容易、性能更好，但序列化/编辑器/层级表达成本极高，且与"对标 Godot 的学习目标"偏离。

**后果**：+ 工具链与序列化天然顺；− 大批量同质实体性能不如 ECS（本项目没有这种场景，文字游戏 + 简单渲染 demo）；− 需要在文档里反复讲清边界，否则代码会长成两不像。

**什么情况下改回来**：如果 M8/M10 的 Demo 需要上万同质实体（弹幕/群体 AI），则把该类实体整体迁到 SoA 层（S3）。

---

## D3 · 句柄 = index + generation（SlotMap），与全局 ObjectID 并存
**状态**：暂定 · **里程碑**：M1~M2

**决策**：两套 ID 并存，职责分明：
- `Handle<T>`：**表内**引用（`SlotMap<T>` / `SparseSet`），带类型，编译期防混用。位布局暂定 index 24 + generation 40（64-bit）。
- `ObjectID`：**全局**引用（`ObjectDB` 查 `Object*`），用于跨模块、事件目标、延迟调用、WeakRef。

**理由**：Godot 也是这样分（`RID` 走各 Server 的 `RID_Owner`，`ObjectID` 走全局 `ObjectDB`）。强行统一会牺牲一方的性能或类型安全。

**后果**：+ 各自的性能与安全都最优；− 概念数量 +1，新人（含 3 个月后的你）容易混 → 必须在 `01-architecture.md` §4.1 与两个类型的头文件注释里各放一张对照表。

**待定子问题**：generation 位宽（40 bit 是否浪费？32/32 是否够？）、`Handle<T>` 是否允许 `Handle<const T>`、哈希函数选择 → 待 benchmark E1 实测后决定；generation 回绕见 START-HERE R5（接受，不测）。

---

## D4 · 自研最小数学库，GLM 只在 Vulkan 后端用
**状态**：暂定（对应 `01-architecture.md` Q1）· **里程碑**：M4（原定 M1）

> **2026-09-26 修订**：里程碑从 M1 推迟到 M4。理由：M1 里没有数学库的消费者，
> 提前写 `Vec3/Mat4` 只能靠单元测试验证，属于造一个没人用的模块。M4 做 Transform 层级时它才有真实用途（R3）。
> 决策本身（自研而非 GLM）不变。

**决策**：`yr/core/math.h` 提供 `Vec2/Vec3/Vec4/Mat4/Quaternion/Transform3D`（值语义、`constexpr` 友好、无动态分配）。
GLM 只出现在 `engine/render/vulkan/` 与 `apps/render_demo`，在快照边界处做一次显式转换。

**理由**：① 数学库是理解 Transform 层级、脏标记、快照布局的必要练习；② 序列化要精确控制字段与对齐，第三方类型会漏进格式里；③ GLM 的头文件体积与模板膨胀会拖慢整个 Runtime 的编译；
④ **不让 GLM 类型越过 `yr_render_iface` 边界**，正是"解耦"的具体体现（否则 Backend 换了数学库 Runtime 就得改）。

**备选**：*直接用 GLM*（省 1 周，但失去学习价值且引入编译期依赖）；*用 Eigen*（过重）。

**后果**：− M4 多花约 3~5 天；− M10 需要 `Mat4 ↔ glm::mat4` 转换函数（写一次，加测试）；+ Runtime 编译更快、依赖更少。

**范围控制**：只做引擎必需的操作（加减乘、normalize、cross/dot、lookAt、perspective/ortho、TRS 合成、求逆）。
**不做**：SIMD 优化、双精度、几何算法库。这些是 stretch 都算不上的时间黑洞。

---

## D5 · Variant 精简实现（9 种类型），Dict 保序
**状态**：暂定 · **里程碑**：M2

**决策**：第一版只支持 `kNull/kBool/kInt/kFloat/kString/kStringId/kVec3/kArray/kDict/kObjectID`（含 kStringId/kObjectID 共 10 个 tag，9 种"值类型"）。
`Dict` 用**保序 `vector<pair<StringId, Variant>>`**，不用 `unordered_map`。先不做 COW，`Array/Dict` 直接深拷贝。

**理由**：保序是"序列化输出逐字节可复现"的前提（M5 的确定性 round-trip 验收标准依赖它）；
COW 是优化，应该等 E6 实验测出拷贝成本确实成问题后再做（先测量后优化）。

**后果**：+ 实现量可控（Variant 是很容易失控的模块，Godot 的 `variant.h` 三千多行）；− 深层嵌套 Dict 拷贝慢 → 用 E6 量化，必要时加 `SharedPtr` 语义。

**什么情况下改回来**：E6 显示 Variant 拷贝占主循环 >5% → 引入 COW 或 `Array/Dict` 改引用语义（写新 ADR）。

---

## D6 · 反射注册宏限制在 3 个以内，先手写后宏化
**状态**：暂定 · **里程碑**：M2

**决策**：只允许 `YR_CLASS(Type, Parent)` / `YR_PROPERTY(Type, name, member)` / `YR_BIND_METHOD(...)`（后者 stretch）。
实现顺序：**先手写一个不用宏的 `ClassInfo` 注册版本并跑通测试 → 再封装成宏 → 用 `clang -E` 展开验证 → 读懂展开的每一行**（展开结果太长不适合存档，读懂即可）。

**理由**：宏工程是本项目最大的调试黑洞（`02-roadmap.md` M2 常见坑列了 5 条）。先手写能确保你理解宏在生成什么，而不是在"调宏直到编译通过"。

**后果**：+ 可控、可调试、可解释；− 比 Unreal 的 UHT 代码生成方案"低级"（本项目不需要 UHT，那需要一整套 Python 工具链——虽然 M11 可以做一个极简版当 stretch）。

**备选**：*代码生成（Python 脚本扫头文件生成 `.gen.cpp`）*：更接近 Unreal/Godot（`*.gen.cpp`）的真实做法，学习价值高，但 M2 阶段成本过大 → **列入 stretch（S2 的扩展）**，若 M11 有余力可做。

---

## D7 · 渲染解耦 = POD 快照契约 + 单向抽取 + 零回调
**状态**：暂定 · **里程碑**：M9~M10

**决策**：见 `01-architecture.md` §4.11 与 §7。四条硬规则：
① 快照内禁止出现 `yr::scene::*` / `Object*`；② 唯一耦合点是 `yr_engine` 里的 `RenderExtractSystem`（SceneTree → Snapshot，单向）；
③ Backend 禁止回调 Runtime、禁止持有 `SceneTree*`；④ Runtime 侧禁止 include `<vulkan/*>` / `<GLFW/*>`。
`yr_render_iface` 只依赖 `yr_core`，使 ①④ 由构建系统物理保证。

**理由**：这是本项目 M9/M10 的全部学习目标。Yo_Renderer 的现状（`render(items, vp, cameraPos, const void* lightsData, count, frame)`，即 V6）
是"没有契约的契约"——`void*` 让类型安全消失、参数列表让扩展必须改签名。快照结构体解决了这两点。

**验证手段**（比声明更重要）：NullBackend 在无 GPU CI 上跑全部测试；快照 dump → replay 逐字节一致；grep 门禁。

**后果**：+ 后端可替换、可测试、可独立线程；− 每帧要拷贝一份快照（用 E14 量化，1k~10k DrawItem 级别可忽略）；− 快照字段变更要同时改两侧（这正是"契约"应有的摩擦）。

**什么情况下改回来**：若 E14 显示 extract+copy 成为瓶颈 → 改为"命令流 + 双缓冲环形队列"（对照 Godot `command_queue_mt.h`），写新 ADR。

---

## D8 · 线程模型：逻辑单线程，工作线程只碰 POD 与不可变数据
**状态**：暂定 · **里程碑**：M6~M7

**决策**：铁律——**任何 `Object` 的构造与析构、任何 `SceneTree`/`ObjectDB`/`ClassDB` 的写操作，只发生在主线程。**
工作线程只能：读不可变数据、处理 POD、做 IO 与解码。跨边界方式只有两种：任务（`JobSystem`）与快照（`RenderSnapshot`）。
`ClassDB` 启动后 `freeze()`，工作线程只读。

**理由**：这条规则把并发 bug 的可能面缩小了一个数量级，且与 Godot `ResourceLoader` 的实际做法一致（IO/解码在线程，对象构造回主线程）。
在还不会熟练使用 TSan 的阶段，规则比技巧重要。

**后果**：− 无法把游戏逻辑本身并行化（本项目不需要）；− 异步加载的"最后一公里"必须在 `pump()` 里同步执行，可能产生帧尖峰（用 E10 量化，必要时分帧摊销）。

**演进路径**：M6 先做"假异步"（入队 + pump 同步执行）→ M7 换真线程 → M10 考虑独立渲染线程（S5）。每次演进写新 ADR。

---

## D9 · 序列化格式自研 `.yrscn`（文本）+ `.yrscnb`（二进制），JSON 仅调试
**状态**：暂定（对应 Q4）· **里程碑**：M5

**决策**：文本格式自研（类 `.tscn` 结构：段头 + key = value，见 `01-architecture.md` §4.9 样例）；二进制格式 `.yrscnb`（magic + version + 长度前缀）；JSON 仅作导出/调试。
**先把格式写下来**（在对话里过一遍：段结构、引用怎么表达、错误怎么报），定稿后写进 `yr/serialize/` 的头注释，**再写解析器**。

**理由**：格式设计本身就是学习目标（引用、版本、循环三大难题）；自研文本格式可 diff、可手写、可版本化，这些性质 JSON 也能给，但自己写一遍才能理解 Godot `.tscn` 为什么长那样。

**备选**：*直接用 JSON*（省 1~2 周，但失去手写 lexer/parser 与格式设计的练习，且 JSON 表达"引用/子资源"很别扭）；*用 Protobuf/FlatBuffers*（引入依赖与代码生成，且看不到内部机制）。

**后果**：− M5 是全部里程碑里最容易超期的一个（已在 roadmap 给降级方案：二进制推到 M11）；+ 格式完全可控，round-trip 确定性可达成。

---

## D10 · 场景结构修改收敛到帧阶段 9
**状态**：暂定 · **里程碑**：M4

**决策**：帧阶段 5~8（pre_physics / physics / process / post_process）内**只能改数据**；
`add_child` / `remove_child` / `queue_free` 全部进待处理队列，由阶段 9 `apply_structure` 统一执行。
新增节点本帧不参与 process（与 Godot 一致，用双缓冲节点列表实现）。

**理由**：一次性消灭"遍历中修改容器"这类 bug，并让阶段 8 的并行化成为可能（没人改结构 → 无数据竞争面）。

**后果**：+ 语义确定、可测试、可并行；− 有一帧延迟（"我 add_child 之后立刻 get_node 拿不到"）→ 提供 `add_child_now()` 供**非遍历期**（如加载阶段、阶段 9 之后）使用，并加 assert 防止在遍历期调用。

---

## D11 · 分层 = 分层 CMake target，依赖方向由构建系统与 CI 强制
**状态**：暂定 · **里程碑**：M0

**决策**：11 个 target（`01-architecture.md` §2），依赖只向下；`tools/check_deps.py` 在 CI 里扫描 include 图，违反即 fail；同时扫描禁用符号（裸 new/delete、非白名单目录的 vulkan include）。

**理由**：Yo_Renderer 的 V1~V4 证明"靠自觉"必然失败（`core` 反向 include `render`）。**规则必须有机制保证，而不是靠自律**（该文档 §0 的原话，继续用）。

**后果**：− M0 多花 1~2 天写脚手架与检查脚本；− 新增一层要改 CMake（可接受）；+ 之后 8 个月的架构腐化速度显著降低。

---

## D12 · Linux-first，平台层保留薄抽象但不移植
**状态**：暂定 · **里程碑**：全程

**决策**：只在 Linux（本机 **Linux Mint 22.3 "Zena"**，基座 Ubuntu 24.04 noble / GCC 13 / Clang 18）开发与测试。平台相关代码集中在 `yr/platform/`（headless 实现 + M10 的 GLFW/Xlib 实现），但不做 Windows/macOS 移植（列入 S12）。

**理由**：移植的学习价值远低于同等时间投入在 Runtime 核心上；且 Yo_Renderer 已标注"当前仅支持 Linux"。

**后果**：+ 无需处理路径/编码/线程/DLL 的平台差异；− 路径分隔符、文件锁、`sleep` 等仍要用可移植写法（`std::filesystem` / `std::this_thread`），避免把 Linux 假设写死进 core。

---

## D13 · 测试用 Catch2（系统包优先 + FetchContent 回退），benchmark 独立且不进 CI
**状态**：暂定 · **里程碑**：M0

**决策**：`find_package(Catch2 3 QUIET)` 优先用系统包（本机 3.7.1），**找不到则 FetchContent 回退**（pinned v3.7.1）。
benchmark 放 `benchmarks/` 独立 target，**手动运行**，结果集中写 `benchmarks/README.md`。

**理由**：系统包冷启动快；benchmark 在 CI 上噪声极大（共享 runner），跑出来的数字没有意义，且会拖慢反馈。

**备选**：*GoogleTest*（生态大但本机未装，且 Catch2 的 `SECTION` 更适合行为驱动的测试组织）；*doctest*（Godot 用它，编译更快，可作为将来对照实验）。

**后果**：+ 快、可离线；− 与 Godot 的测试风格不同（读 Godot 测试时注意它是 doctest 的 `TEST_CASE`/`ERR_FAIL_*` 宏）。

**复盘（2026-09-22，Day 4b）**：本决策隐含的前提"系统包到处都有"**在 CI 上不成立**。
Ubuntu 24.04（noble）的 apt 源里**没有** `libcatch2-dev` —— 本机那个 3.7.1 是从别处装的
（`apt-cache policy` 只显示 `/var/lib/dpkg/status`，无任何仓库来源，这就是证据）。
CI 首次运行时 `apt-get install libcatch2-dev` 退出码 100，job 失败。

修正（决策本身不变，Catch2 仍优于 GoogleTest）：
1. `tests/CMakeLists.txt` 改为 `find_package(Catch2 3 QUIET)` + **FetchContent 回退**（pinned `v3.7.1`、`GIT_SHALLOW TRUE`），
   并显式 `list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")` 让 `include(Catch)` 在两条路径下都能工作。
2. CI 的 apt 步骤把 `libcatch2-dev` 拆成单独一行并容错（装不到只发 warning）。
3. 代价：CI 的 configure 阶段要多花约 1~2 分钟克隆并构建 Catch2 → Day 4c 用 Actions 缓存 + ccache 缓解。

**教训（比这个 bug 本身值钱）**：原理由里写的"CI 无需网络"是想当然 —— 我并没有在干净环境里验证过。
**"本地能构建"和"别人能构建"是两件事**，只有 CI 能告诉你后者。以后凡是写进 ADR 的理由，
凡是涉及"某环境里有某个东西"的断言，都要么验证过、要么标注为假设。

---

## D14 · 不使用异常做控制流
**状态**：暂定 · **里程碑**：全程

**决策**：`engine/` 内禁用异常（`-fno-exceptions` 可选，先不禁用以保留第三方库兼容性）；错误经 `std::optional` / `Expected<T, Error>` / bool + 日志上报；
不变量破坏用 `YR_ASSERT`；启动期与 `tools/` 可用异常，顶层统一 catch。

**理由**：游戏引擎主流做法（Godot/Unreal 均不用异常做控制流）；异常在热路径的成本与"错误路径是否被测试过"的问题都很难处理；
断言 + 返回值的组合让错误处理显式可见。

**后果**：− 需要自己写 `Expected` 或大量用 `optional`（M0 决定：先 `optional` + 日志，不够再写 `Expected`）；− 构造函数无法报错 → 用两阶段初始化（`create()` 返回 `optional`/`Ref`）或工厂函数。

---

## D15 · Vulkan 后端由 Yo_Renderer 搬入并改名空间，而非 submodule 引用
**状态**：暂定（前置假设 A3）· **里程碑**：M10

**决策**：M10 时把 Yo_Renderer 的 `platform/`（窗口+Vulkan context+swapchain）、`rhi/`、`render/`、`asset/`(gltf/hdr loader) 搬进 `engine/render/vulkan/`，
统一放入 `yr::render::vk` 命名空间，并**先原样跑通再重构**。
**不搬**：`framework/`（Level/Entity/Component）、`system/`（Transform/Camera/Batch/Light）、`ui/`（ImGui panels）、`core/`（Ref/Handle/SparseSet/ObjectDB）——这些由 YRuntime 的对应层取代。

**理由**：submodule 引用会让 YRuntime 受旧架构（V1/V2 反向依赖）约束，解耦练习打折；
搬入 + 删除被取代的部分，正好强制你面对"哪些是渲染、哪些是运行时"这条线——**这个分类过程本身就是 M10 最大的学习产出**。

**后果**：− 4~6 周工作量（本项目最长单一里程碑）；− Yo_Renderer 与 YRuntime 出现代码重复（接受，Yo_Renderer 归档为独立作品）；
+ 可顺手修掉 V5/V6（光照布局三份副本、`void*` 参数），且 V6 由快照契约天然解决。

**风险控制**：搬迁与重构必须分开 commit（`refactor(vulkan): 搬运，无逻辑变化` → `feat(vulkan): 接入快照契约`）；
每步之后跑 Yo_Renderer 原有的 `YoCoreTests` 等价物 + 画面冒烟。

---

---

## D16 · Sanitizer 与 Debug 构建分离，而不是合并
**状态**：暂定 · **里程碑**：M0（Day 3c 讨论后确定）

**背景**：实现 `asan` preset 时提出的疑问——"debug 本来就是开发期用的，为什么不把 sanitizer 直接开在 debug 里？"
这个方案确实有项目在用，所以值得正式记录取舍。根源是一个概念混淆：
**Debug 构建（`-O0 -g`）的目的是"让调试器看得清"，sanitizer 的目的是"让隐藏错误显形"**——两件不同的事，只是都在开发期用。

**决策**：保持分离。`debug` preset 不含 sanitizer；`asan` preset = Debug + `YR_ENABLE_SANITIZERS=ON`。
工作流：写代码用 `debug` → **提交前**跑 `asan` → 测性能用 `release` → M7 起查竞争用 `tsan`。

**理由**（按对本项目的实际影响排序）
1. **benchmark 会失效**：ASan 的 redzone 与插桩开销约 2 倍且**不均匀**，会掩盖 `03-learning-map.md` E1~E16 想测量的 cache 行为与内存布局差异。M1 就要跑 E1，必须有干净的 Debug。
2. **干扰 gdb**：ASan 拦截 `malloc/free`、安装自己的信号处理器、改变内存布局 → 单步会跳进 sanitizer 内部、watchpoint 异常、崩溃点定位需要额外配 `ASAN_OPTIONS=abort_on_error=1`。
3. **拖慢迭代**：插桩增加编译时间，链接多链 `libasan`。项目变大后（M4 起）日常反馈回路明显变长。
4. **第三方噪音**：LeakSanitizer 会报告修不了的泄漏（Catch2 内部、将来的 GLFW/Vulkan 驱动），只能 suppress；噪音过多会让人对真报告麻木，比没有 sanitizer 更糟。
5. **TSan 与 ASan 互斥**：分离的 preset 天然容纳 M7 的 TSan；若 debug 默认开 ASan，反而要多做一个"关 ASan"的开关。
6. **CI 矩阵需要两者**：`debug`（快，覆盖逻辑）与 `asan`（慢，覆盖内存）各是一个 job。

**备选与否决理由**
- *debug 默认开 ASan*：省去"忘记跑 asan"的风险，但代价是上面 1~6 全部发生，且 benchmark 无处可跑。
- ***debug 只开 UBSan（开销约 1.2 倍，几乎不干扰 gdb，不做泄漏检查），ASan 单独 preset***：
  **这是最值得考虑的折中**，日常就能抓到整数溢出/空指针解引用，同时避开 ASan 的全部代价。部分项目采用此方案。
  暂不采用，理由见"什么情况下改回来"。

**后果**：+ 日常迭代快、benchmark 可信、gdb 好用、CI 覆盖两类问题；
− 存在"忘记跑 asan"的风险 → 两道兜底：`05-engineering.md` §10 提交前自检清单，以及 Day 5 的 CI（asan 是矩阵中一个 job，跑不掉）。

**什么情况下改回来**：若**连续两次**出现"本地自检通过、提交后 CI 才发现内存错误"（说明自检清单在实践中失效），
就改为备选方案 3（debug 并入 UBSan）。用事实触发，而不是用担心触发。

---

---

## D17 · 仓库内不产出笔记文件，记录只靠 commit message + ADR + checkbox
**状态**：已确认 · **里程碑**：M0（2026-09-22）

**背景**：规划期设计了 5 类过程记录（周记 / 设计笔记 / Godot 对照笔记 / benchmark 报告 / evidence），
散落在 120+ 处文档引用里。M0 实践中（Day 1~3c，4 天）**五类产出全部为 0，被提醒两次后仍为 0**，
而同期技术任务全部达标且快于估算、还主动补了 clangd 等未被要求的工作。

诊断结论：**不是执行力问题，是激励结构问题**——记录成本 ≈ 编码成本，回报却在 3 个月后。
任何"高频 + 延迟回报"的任务都会被系统性跳过，靠提醒无法解决。

**决策**：删除 `doc/notes/` 全部结构与模板。记录只保留三种载体：
1. **commit message 正文**——写"为什么这么改"。高频、寄生在已有动作上、成本接近零。
2. **`06-decisions.md` 的 ADR**——只收架构级取舍（改变模块边界，或改起来很贵）。
3. **`02-roadmap.md` 的 checkbox + 日期**——进度。

离开代码就会失效的细节（内存序选择、帧阶段顺序、快照字段的单位与矩阵序）写进**代码注释**，不写进文档。
章节性总结由作者自行整理，**不放进仓库**。

**备选与否决理由**
- *保留周记但降低频率*：仍是"额外动作"，仍会被跳过。
- *把模板从 43 行砍到 12 行*：成本降低但没归零，还增加了"该写多详细"的判断负担。
- *由 AI 从对话自动生成笔记*：产出的是 AI 的理解而非作者的理解，直接违反 `05-engineering.md` §9.1「必须自己写才能讲清」的要求。

**后果**：+ 记录成本归零，不再有系统性跳过；+ 文档数量与体积可控（可读性、可浏览性）；
− **"为什么"的信息只存在于 commit message 里** → 所以 commit message 的标准要提高（`00-vision.md` §4.5 已加对应验收项）；
− `07-portfolio.md` §2 的"证据链②：设计笔记时间戳早于代码"不再成立 → **2026-09-26 已修订**：该节改为「理解自检」（五个自问问题 + 四个只做不写的动作），不再要求任何笔记产出。

**什么情况下改回来**：若发现 `git log` 读下来无法还原设计演进（即 commit message 也不够），
说明需要更强的记录载体 —— 届时优先考虑"每里程碑一篇 ≤15 行总结"，而不是恢复周记。

---

## D18 · 项目的检验标准是"能否讲清楚"，不是"能否自证"
**状态**：已确认 · **里程碑**：M0（2026-09-26）

**背景**：`07-portfolio.md` §2 原本设计了四条"证据链"，要求产出维持"这是我自己做的"的证据：
① 设计笔记的时间戳必须早于代码；② 录 5~20 分钟讲解视频留证；③ 以"至少 2 条 ADR 被推翻"为指标；
④ 用 commit 数量衡量可信度。这套设计在 Day 1~3c（4 天）产出为 0——与 D17 记录的现象完全一致。

更根本的问题是：**这套规则把学习项目变成了表演项目。** 它会让人为"看起来像独立完成"做事，
而不是为"弄懂"做事；把"犯错"变成 KPI 甚至可能诱导为了推翻而推翻。

**决策**：**删掉全部"自证"要求。** 检验标准改为一条：**能不能不看代码把它讲清楚。**

- `07-portfolio.md` §2 改为「理解自检」：五个自问问题（为什么存在 / 边界 / 失败模式 / 与 Godot 的差异 / 推翻过什么）+ 四个"只做不写"的动作。
- 不设任何记录产出指标（不要求笔记篇数、不要求 ADR 数量、不要求视频）。
- 面向外部的部分（简历条目、开源合规）保留，但明确是**理解到位后的副产品**。

**为什么这反而更可信**：能脱稿讲清原理、能说出自己踩的坑与推翻的设计，比一份齐全的证据文件更难伪造，
也是面试官真正考察的东西。文件可以补，理解补不了。

**备选与否决理由**
- *保留证据链但降低要求*（例：只要 1 篇笔记）：仍是额外动作，仍会被跳过（D17 已证）；且它治不了"表演化"这个根本问题。
- *完全删掉 portfolio 文档*：面试题库（§5 的 40 题）和展示物清单仍有实际价值，保留。

**后果**：+ 评价标准从"产出物数量"回到"理解深度"；+ 消除 `notes/` 死引用与 D17 的矛盾；
− 失去了"时间戳可验证"这种低成本的外部信号 → 但那个信号本来也没被产出过，且可被有意构造，价值有限。

**什么情况下改回来**：如果要正式投递实习/求职且需要对外材料，
再按目标岗位的要求单独准备一份**作品说明**（不在本仓库的日常流程里）。

---

## 附：ADR 模板（复制使用）

```markdown
## Dxx · <一句话标题>
**状态**：暂定 | 已确认 | 已推翻（被 Dyy 取代） · **里程碑**：M?

**背景**：<遇到什么问题，为什么现在必须决定>

**决策**：<具体做什么，越具体越好>

**备选与否决理由**：
- <方案 B>：<为什么不选>
- <方案 C>：<为什么不选>

**后果**：+ <好处>；− <代价/新风险>

**什么情况下改回来**：<可观测的触发条件，最好是某个 benchmark 数字或某类 bug>

**复盘**（实施后回填）：<实际与预期的差异，日期>
```
