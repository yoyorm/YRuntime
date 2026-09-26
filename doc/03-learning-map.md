# 03 · 学习地图

> 生成于 2026-09-19。本文回答："**在这个项目的哪个阶段，我需要掌握什么知识，学到什么程度，从哪里学**。"
> 与 `02-roadmap.md` 配合使用：roadmap 说"做什么"，本文说"为此要懂什么"。
>
> **总原则（2026-09-26）**：这是学习项目，**理解 > 完成 > 展示**。
> 允许某一步做得比别人差，但不允许"代码能跑却讲不清为什么"。
> 判断自己有没有真懂，用 `07-portfolio.md` §2 的五个自问问题；不需要写笔记（D17）。

---

## 0. 学习方法（比清单更重要）

| 方法 | 具体做法 | 为什么 |
|---|---|---|
| **先造轮子再读源码** | 自己写完 `SlotMap` → 再读 Godot `rid_owner.h` → 写下差异 | 先有"我的方案"，读源码才有参照系，否则只是抄 |
| **费曼式验收** | 每个模块完成后，对着空白纸讲 5 分钟，讲不下去的地方就是没懂 | 也可以直接在对话里讲给 AI 听、让它挑毛病——效果相同、成本更低 |
| **用实验回答问题** | 每个"我不确定 X 是否重要"的疑问 → 写一个 benchmark 或最小复现，用数据回答（见 §4） | 引擎开发的判断力来自测量，不来自直觉或博客 |
| **讲出来倒逼理解** | 每完成一个模块，用 5 分钟把"为什么存在/边界/失败模式/与 Godot 的差异"讲一遍 | 讲不出来 = 没懂。讲给 AI 听成本最低、反馈最快 |
| **写博客（可选）** | 有想说的话时写一篇（`07-portfolio.md` §4）。**不设篇数 KPI** | 写不出来说明还没想透，但这不是欠债，不必硬写 |
| **对照阅读法** | `04-godot-study.md` 的"Godot 怎么做 / 我怎么做 / 为什么不同"三段式 | 避免玩具化，也避免盲目模仿 |
| **读头文件优先** | 读 Godot 时**只读 `.h` 与关键 `.cpp` 片段**，不要试图读完 | 14GB 源码读不完；接口即设计意图 |

**读源码的时间盒**：单次 ≤60 分钟，读完必须能说出 3 句结论（在对话里说就行，不落文件——D17）。说不出结论就是读错了文件。

---

## 1. C++ 知识点 → 里程碑映射

"程度"栏：`A` = 会用即可 / `B` = 要能解释原理与代价 / `C` = 要能设计并教别人。

| 知识点 | 用在 | 程度 | 学什么 | 资料 |
|---|---|---|---|---|
| CMake 现代用法（target、`target_*` 传播、generator expression、preset、FetchContent） | M0 | **C** | PUBLIC/PRIVATE/INTERFACE 的区别与传染；为什么不用 GLOB | 官方 cmake-tutorial、《Professional CMake》(Craig Scott) 前 12 章 |
| 编译/链接模型（TU、ODR、静态库、符号可见性、SIOF） | M0, M2 | **B** | 静态注册器为什么可能丢符号；`--whole-archive` | cppreference、yo_lib 的 `yo_export.h` 就是可见性练习 |
| RAII / 五法则 / 移动语义 | 全程 | **C** | 什么时候写 move ctor；`noexcept` move 与容器增长 | 《Effective Modern C++》Item 17-24, 29, 41 |
| 模板与 `if constexpr` / concepts / 变参模板 | M1, M2, M3 | **B** | SFINAE vs concepts；变参展开 | cppreference、《C++ Templates》第 2 版 前 8 章（按需查） |
| 宏工程（`__VA_ARGS__`、`__VA_OPT__`、token pasting、多行宏、宏内声明静态变量） | M2 | **B** | 为什么所有引擎都在用宏做反射；宏的可调试性代价 | Godot `GDCLASS`、Unreal `UCLASS`（读，不抄） |
| 类型擦除（函数指针 + void* context、`std::function`、成员指针） | M2, M3 | **C** | 三种擦除方式的体积/性能/可读性对比 | `core/object/callable_mp.h`、`method_bind.h` |
| tagged union / `std::variant` / 手写 Variant | M2 | **B** | 为什么引擎不用 `std::variant` | Godot `variant.h` |
| 智能指针与侵入式引用计数 | M2, M6 | **C** | intrusive vs non-intrusive；控制块开销；`enable_shared_from_this` | 《Effective Modern C++》Item 18-22；Godot `ref_counted.h` |
| 原子与内存模型（`memory_order_*`、happens-before、false sharing） | M3, M7 | **B** | acquire/release 配对；为什么 stats 要 `alignas(64)` | 《C++ Concurrency in Action》2nd 第 5、7、9、10 章 ← **M7 前必读** |
| 线程池 / 任务图 / work stealing | M7 | **B** | 任务粒度；主线程帮忙执行 | Godot `worker_thread_pool.cpp`；Folly/EnkiTS 源码选读 |
| 条件变量与 lost wakeup | M7 | **B** | 为什么 `wait` 必须带 predicate | 《C++ Concurrency in Action》第 4 章 |
| `std::chrono` 与时间语义 | M1, M4 | A | steady vs system clock；duration cast 精度丢失 | cppreference |
| 缓存友好数据布局（SoA/AoS、稠密数组、对齐） | M1, M4, M7 | **B** | 为什么遍历比查找更值得优化 | 《Data-Oriented Design》(Richard Fabian) 前 3 章；Mike Acton "Data-Oriented Design and C++" (CppCon 2014) |
| 手写 lexer/parser（递归下降） | M5 | **B** | token 化、错误位置报告 | 《Crafting Interpreters》第 6 章（扫描器）即可 |
| 二进制序列化（对齐、字节序、版本头、`static_assert(sizeof)`） | M5 | **B** | 结构体填充陷阱；为什么不能直接 `fwrite(struct)` | Godot `resource_format_binary.cpp` |
| POD 契约 / ABI 稳定 | M9 | **B** | 为什么跨模块边界传 POD 而不是类 | — |
| Vulkan（已有基础）：dynamic rendering、sync2、descriptor、frame-in-flight、延迟销毁 | M10 | **B** | 你已会；重点转向"如何被 Runtime 驱动" | 你自己的 Yo_Renderer + vkguide.dev |

**不需要学的**（明确排除，防止时间浪费）：C++23 新特性（本项目固定 C++20）、异常安全的全部细节（不用异常做控制流）、
协程（M6 的异步用回调 + 状态机足够，协程列入 stretch 之外的"永不"）、模板元编程炫技。

---

## 2. 引擎架构知识点 → 里程碑映射

| 主题 | 用在 | 要能回答的问题（自测） | 主要资料 |
|---|---|---|---|
| 游戏循环与时间管理 | M4 | 固定步长 vs 可变步长的取舍？spiral of death 怎么防？插值渲染怎么做？ | 《Game Engine Architecture》(GEA) 第 8 章；Glenn Fiedler "Fix Your Timestep" |
| 对象模型：GameObject/Component 的三种流派 | M2, M4 | 继承树 / 组件组合 / ECS 各自的适用场景与代价？ | GEA 第 22 章（原文按版本可能不同，找"Gameplay Systems"章）；Scott Bilas "Data-Driven Game Object System" (GDC 2002)；EnTT wiki "ECS back and forth" |
| 属性系统与反射 | M2 | 反射能换来什么？代价是什么（编译时间、二进制体积、调试难度）？ | Godot `class_db.h`；Unreal UPROPERTY 文档 |
| 序列化与资产管线 | M5, M6, M11 | 为什么引擎需要"导入/cook"这一步？为什么不能直接读源文件？ | GEA 资产管理章；Godot `resource_loader.cpp`、`file_access_pack.h` |
| 资源生命周期与流式加载 | M6 | 引用计数 vs GC vs 显式卸载？异步加载为什么必须回主线程收尾？ | Godot `resource_loader.cpp`（ThreadLoadTask）；UE 的 Async Loading |
| 事件/消息/信号 | M3 | 立即派发 vs 队列派发的差别？为什么引擎两者都要？ | Godot `message_queue.h`；GEA 事件系统章 |
| 并发架构（游戏线程/渲染线程/工作线程） | M7, M9, M10 | 引擎为什么普遍是"单线程逻辑 + 并行数据 + 独立渲染"？边界怎么画？ | GEA 并发章；Godot `server_wrap_mt_common.h`；《Multiplayer Game Programming》(选) |
| 场景图与层级变换 | M4 | 脏标记怎么传播？为什么很多引擎缓存世界矩阵？ | Godot `node.cpp` 的 transform propagation；Yo_Renderer roadmap §1.2 |
| Server 模式与渲染解耦 | M9, M10 | 为什么 Godot 把渲染/物理/音频都做成 Server + RID？ | Godot `servers/`；Frostbite FrameGraph (GDC 2017)；Unreal RDG 文档 |
| 构建系统与模块化 | M0, M11 | 为什么大引擎构建系统那么复杂？SCsub/CMake 各解决什么？ | Godot `SConstruct`+`methods.py`；UE 的 UBT 概念文档 |
| 调试与剖析 | M11 | 没有 profiler 时怎么定位性能问题？ | `perf` 文档；Tracy 文档；GEA 调试章 |

**主线教材**：《Game Engine Architecture》Jason Gregory（第 3 版）。
**读法**：不要顺序通读。按里程碑跳读——M1 读内存/容器章，M2 读对象模型章，M4 读游戏循环章，M6 读资源章，M7 读并发章。
每章读完写 5 条"我要在项目里验证的结论"，然后在对应里程碑里验证。

---

## 3. 阅读材料清单（按优先级）

### P0（本项目期间必须读）
| 材料 | 用途 | 何时 |
|---|---|---|
| Godot 4.8 源码（本机 `/home/yoyorm/Code/GodotDev/godot`） | 参照系 | 全程，见 `04-godot-study.md` |
| 《Game Engine Architecture》3rd | 主线教材 | 按章跳读 |
| 《C++ Concurrency in Action》2nd 第 4/5/7/9/10 章 | M7 前置 | M6 结束时 |
| 《A Philosophy of Software Design》(John Ousterhout) | 模块设计判断力（薄，~180 页） | M0~M2 期间读完 |
| Glenn Fiedler "Fix Your Timestep" | M4 固定步长 | M4 开工前 |
| 你自己的 `Yo_Renderer/doc/*.md` | 反面教材 + 已有成果 | M0、M9、M10 |

### P1（强烈推荐）
| 材料 | 用途 |
|---|---|
| 《Effective Modern C++》Meyers | 随时查，重点 Item 1-10, 17-24, 29-41 |
| EnTT wiki "ECS back and forth" 系列 | S3（ECS 数据层）与 sparse set 深度理解 |
| Mike Acton, "Data-Oriented Design and C++" (CppCon 2014) | 数据布局思维（1 小时视频，值得看两遍） |
| 《Data-Oriented Design》Richard Fabian（免费在线） | 前 3 章 |
| Godot 官方文档 "Multiple threads" / "Using Servers" | Server 模式与线程模型 |
| vkguide.dev | M10 复习 Vulkan 工程实践 |

### P2（按需/stretch）
| 材料 | 用途 |
|---|---|
| 《Crafting Interpreters》（免费在线） | S1 脚本 VM |
| Frostbite "FrameGraph" GDC 2017 | S6 |
| 《Professional CMake》 | M0/M11 构建系统深入 |
| 《Game Programming Patterns》(Robert Nystrom，免费在线) | 模式速查（Component/Event Queue/Object Pool/Service Locator 与本项目直接相关） |
| Tracy Profiler 文档 | M11 |
| 《Computer Systems: A Programmer's Perspective》 | 你已在读（CS-APP 目录）；cache/链接/虚拟内存章节与 M1/M7 直接相关 |

---

## 4. 实验清单（用数据回答问题）

这些实验是本项目"能力展示"的硬核部分——**能拿出自己测的数据，比引用别人的结论有说服力得多**。
所有实验的结果集中写在 **`benchmarks/README.md`**（一个文件，按 E1~E16 分节）：问题 / 方法 / 数据表 / 结论 / 对设计的影响。
不单独建文件——文档要有限、可浏览（ADR D17）。

| # | 实验 | 对应 M | 要回答的问题 |
|---|---|---|---|
| E1 | SlotMap vs `unordered_map<uint32_t,T>` vs `vector<T>+freelist` | M1 | 插入/查找/遍历各差多少？cache-miss 差多少（`perf stat`）？ |
| E2 | `SparseSet` vs `unordered_map` 做组件存储 | M4/S3 | 稠密迭代收益；稀疏度多高时收益消失？（**只有在真要做 S3 时才做**，见 D4） |
| E3 | `std::function` vs 函数指针+context vs 模板回调 | M3 | 一次事件派发的开销；1000 订阅者 × 1000 帧的总耗时与堆分配次数 |
| E4 | `StringId` 驻留 vs 每次 `std::string` 比较 | M1/M2 | 属性查找快多少？驻留表内存占用？ |
| E5 | 反射属性读写 vs 直接成员访问 | M2 | 反射的运行时开销倍数（这是"反射值不值"的核心数据） |
| E6 | Variant 体积与拷贝成本（含 Array/Dict 深拷贝） | M2 | `sizeof(Variant)`；COW 值不值得做？ |
| E7 | 树遍历策略：递归 vs 显式栈 vs 预排序扁平数组 | M4 | 10k 节点树的 `_process` 派发耗时 |
| E8 | Transform 脏标记 vs 每帧全量重算 | M4 | 不同"每帧移动节点比例"下的收益曲线（Yo_Renderer roadmap §1.2 的正式答案） |
| E9 | 文本 vs 二进制场景格式 | M5 | 1000 节点场景：文件大小、save 耗时、load 耗时 |
| E10 | 同步 vs 异步资源加载（配 ThrottledFileAccess） | M6 | 帧时间 p95 对比；加载 100 个资源的总时长 |
| E11 | JobSystem 加速比 vs 任务粒度 | M7 | 找到"并行收益 = 0"的任务大小区间 |
| E12 | false sharing 实测：`JobStats` 加/不加 `alignas(64)` | M7 | 差多少倍？（经典实验，结果通常惊人） |
| E13 | memory_order relaxed vs acquire/release vs seq_cst | M7 | x86 上差别有多大？为什么 ARM 上不一样（查资料回答）？ |
| E14 | 快照抽取（RenderExtract）耗时 vs 场景规模 | M9 | 1k/10k 节点下 extract 占一帧多少比例？ |
| E15 | 视锥剔除串行 vs 并行（job） | M10 | 收益与开销平衡点 |
| E16 | `.yrpak` vs 散文件加载 | M11 | 冷启动时间对比（打包的真实收益） |

> **规则**：每个实验必须有**方法描述**（硬件、编译选项、迭代次数、是否预热、如何防止被优化掉），
> 否则数据无意义。用 `perf stat` / `hyperfine` / Catch2 `BENCHMARK` 都可以，但要在报告里写清用了什么。

---

## 5. 两种对象范式对照（本项目必须形成的判断）

这是本项目最有面试价值的一个认知点。你的 Yo_Renderer 用了 ECS，YRuntime 主体用 Object 树 + 反射，
**你必须能解释为什么两个项目做了不同选择**。

| 维度 | Object 树 + 反射（Godot/Unity 传统） | ECS / SoA（EnTT/flecs/Bevy） |
|---|---|---|
| 数据布局 | AoS，对象分散在堆上 | SoA/Archetype，组件紧凑连续 |
| 遍历性能 | 差（cache miss 多、虚调用） | 极好（线性扫、可向量化） |
| 表达层级/组合 | 天然（父子树、`get_node("A/B")`） | 需要额外 parent 组件与遍历逻辑 |
| 反射/序列化/编辑器 | 天然（ClassDB 直接驱动 Inspector） | 需要额外注册元数据 |
| 脚本绑定 | 容易（对象即 API） | 较难（要暴露 archetype 查询） |
| 生命周期 | 明确（parent 拥有 child） | 需要 handle/generation 防止悬垂 |
| 并行化 | 难（对象互相引用，数据竞争面大） | 容易（system 声明读写集合，可静态分析） |
| 适合什么 | 玩法逻辑、UI、场景组织、工具链 | 大量同质实体（子弹、NPC、粒子、实例化绘制） |
| 学习成本 | 低 | 中高（思维转变） |

**YRuntime 的答案（写进 ADR D2）**：主体用 Object 树 + 反射（因为项目目标是"理解 Runtime 与工具链"，
且需要序列化/检视器/事件），但在**数据密集且无层级语义**的地方用 SoA：
① Transform 世界矩阵平行数组（M4）；② RenderSnapshot 的 DrawItem 数组（M9）；③ 若做 S3，把"渲染实例数据"整体搬进 SparseSet。
**边界规则**："**逻辑身份用 Object，批量数据用 SoA；Object 持有 index 指向 SoA 行，SoA 不持有 Object 指针。**"

---

## 6. 术语表（中英对照，写文档/博客时统一用词）

| 中文 | English | 本项目里的具体所指 |
|---|---|---|
| 运行时 | Runtime | `yr_engine` + `yr_scene` + 主循环构成的可 tick 环境 |
| 句柄 | Handle | `Handle<T>` = index + generation |
| 槽位表 | Slot Map | `SlotMap<T>` |
| 稀疏集 | Sparse Set | `SparseSet<T>` |
| 字符串驻留 | String Interning | `StringId` |
| 反射 | Reflection | `ClassDB` + `ClassInfo` + `PropertyInfo` |
| 动态值 | Variant | `yr::obj::Variant` |
| 属性系统 | Property System | 属性注册 + 按名读写 |
| 类数据库 | ClassDB | 全局类注册表 |
| 对象数据库 | ObjectDB | `ObjectID → Object*` 全局表 |
| 引用计数 | Reference Counting | `RefCounted` / `Ref<T>` |
| 弱引用 | Weak Reference | `WeakRef<T>` |
| 延迟删除 | Deferred Deletion | `queue_free()` + `flush_deletions()` |
| 延迟调用 | Deferred Call | `MessageQueue::push_deferred` |
| 事件总线 | Event Bus | `EventBus` |
| 通知 | Notification | `Object::notification(uint32_t)` |
| 场景树 | Scene Tree | `SceneTree` + `Node` 层级 |
| 打包场景 | Packed Scene | `PackedScene`（场景的中间表示） |
| 资源 | Resource / Asset | `Resource`（内存对象）/ asset（磁盘文件）—— **两者要分清** |
| 资源数据库 | Asset Database | `AssetDatabase` |
| 内容标识 | UID / Content Hash | `AssetUID` |
| 导入 / 烹制 | Import / Cook | 源资源 → 运行时格式的转换 |
| 打包文件 | Pak / Pack | `.yrpak` |
| 任务系统 | Job System | `JobSystem` |
| 固定步长 | Fixed Timestep | physics accumulator |
| 帧阶段 | Frame Phase | §5 的 14 个阶段 |
| 渲染快照 | Render Snapshot | `RenderSnapshot`（POD 契约） |
| 渲染后端 | Renderer Backend | `RendererBackend`（null / vulkan） |
| 服务 / 服务器 | Server | Godot 术语：`RenderingServer` 这类"接收命令的黑盒服务" |
| 数据竞争 | Data Race | TSan 检测目标 |
| 伪共享 | False Sharing | `alignas(64)` 解决 |
| 静态初始化顺序惨案 | SIOF | 显式 `register_*_types()` 解决 |

---

## 7. 每个里程碑的"知识就绪"检查

进入某里程碑前，确认下表的知识已经补齐（没有就先补，别边做边学核心概念——那会做出错误的架构）：

| M | 进入前必须已懂 |
|---|---|
| M0 | CMake target 与 PUBLIC/PRIVATE 传播；Ninja；ctest |
| M1 | 移动语义与五法则；位运算；cache 基本概念 |
| M2 | 宏展开机制（会用 `clang -E`）；成员指针；类型擦除的三种做法 |
| M3 | `std::function` 的代价；迭代器失效；重入概念 |
| M4 | 组合模式；树遍历（前/后序）；固定步长（读完 Fix Your Timestep） |
| M5 | 递归下降解析基本写法；结构体对齐与填充；版本兼容策略 |
| M6 | 回调 vs future；引用计数语义；IO 抽象与依赖注入 |
| M7 | **《C++ Concurrency in Action》第 5 章（内存模型）+ 第 9/10 章**；条件变量 predicate |
| M8 | 无新知识（这是"用引擎"的阶段，重点在玩法与数据组织） |
| M9 | POD/ABI 概念；双缓冲；Server 模式（读 Godot `servers/` 概览） |
| M10 | 你已有的 Vulkan 知识 + frame-in-flight 与延迟销毁 |
| M11 | 打包格式基本概念（读 Godot `file_access_pack.h`） |
