# 02 · 里程碑路线图（M0 ~ M11）

> 生成于 2026-09-19。基于 `00-vision.md` 假设 A1（学期中 5~8h/周 + 假期冲刺）。
> **使用方式**：每次开工从这里挑 ≤3 个 checkbox；完成后打勾并写日期 `- [x] ... ✅ 2026-10-05`。
> **本文件是进度的唯一来源**（对外进度看根 `README.md` 的里程碑表）。
>
> **关于设计记录**（2026-09-22 定）：每个模块开工前，先在对话里把设计要点过一遍
> （为什么存在 / 职责边界 / 所有权 / 失败模式 / Godot 对照），**仓库内不产出笔记文件**。
> 只有满足 `START-HERE.md` §3 两个条件之一的取舍才写进 `06-decisions.md`；其余理由写进 commit message 正文。

## 里程碑总览

| M | 名称 | 周数 | 新增 target | **可运行物**（必须有，否则不算完成） | 作品价值 |
|---|---|---|---|---|---|
| M0 | 工程地基 | 1 | `yr_core`(壳) `tests/core` | `cmake --preset && ctest` 全绿 + CI 徽章 | ★ |
| M1 | 句柄与容器 | 1~2 | `yr_core` | benchmark 报告：SlotMap vs unordered_map | ★★ |
| M2 | 反射与对象模型 | 2~3 | `yr_object` `yr_inspect` | `yr_inspect --all` 打印所有类与属性 | ★★★★ |
| M3 | 事件与延迟调用 | 1~2 | `yr_event` | 事件重入/顺序测试全绿 + 事件追踪日志 | ★★ |
| M4 | 场景树与主循环 | 3~4 | `yr_scene` `yr_engine` | **headless 世界 tick 10000 帧**（可录屏终端输出） | ★★★★★ |
| M5 | 序列化与场景资源 | 3~4 | `yr_serialize` | 手写 `.yrscn` 加载 + 存读档 round-trip diff | ★★★★★ |
| M6 | 资源系统 | 2~3 | `yr_asset` | 异步加载进度条（模拟慢 IO 不卡帧） | ★★★★ |
| M7 | 任务系统 | 3~4 | `yr_job` | 并行 vs 串行 benchmark + TSan 干净报告 | ★★★★ |
| M8 | 文字冒险 Demo | 2~3 | `apps/text_adventure` | **可玩 ≥10 分钟 + 存读档 + 录屏** | ★★★★★ 作品节点 1 |
| M9 | 渲染契约与空后端 | 2 | `yr_render_iface` `yr_render_null` | 快照 dump 成文件并重放 | ★★★ |
| M10 | Vulkan 后端接入 | 4~6 | `yr_render_vulkan` `apps/render_demo` | **Runtime 驱动画面 + 录屏** | ★★★★★ 作品节点 2 |
| M11 | 工具链与打磨 | 2~4 | `tools/yr_pack` `yr_scene_conv` | `.yrpak` 打包 + 从 pak 启动游戏 | ★★★ |

**总计 ≈ 26~38 周**（约 6~9 个月）。假期按 3 倍速折算。

### 三个铁规则
1. **不跳里程碑**。M2（反射）没做完就开始 M5（序列化）= 返工。唯一例外：M6 可以先做同步加载版本。
2. **每个里程碑的"可运行物"是验收的一部分**，不是可选项。没有可运行物 → 你无法向别人（也无法向 3 个月后的自己）证明它工作。
3. **超期 50% 就砍范围，不砍质量**。每个 M 都给了"降级方案"，用它。

---

## M0 · 工程地基（1 周）

**目标**：让"改一行代码 → 构建 → 测试 → 看到结果"的回路 < 30 秒，并让 CI 替你守住规矩。
这个阶段不写任何引擎逻辑，但它决定了后面 8 个月的开发体验。

### 已完成（Day 1 ~ 6，2026-09-19 ~ 09-26）

- [x] 仓库骨架：`engine/ apps/ tests/ tools/ cmake/ benchmarks/ .github/`、`.gitignore`、`.clang-format`（LLVM 基准 + `ColumnLimit: 120`）、首次 commit、推到 `github.com/yoyorm/YRuntime`（public，SSH）✅ 2026-09-19
- [x] 顶层 `CMakeLists.txt`：3 个 option + 统一输出目录（`build/<preset>/{bin,lib}`）+ `CMAKE_EXPORT_COMPILE_COMMANDS` + `yr_build_flags` INTERFACE target（承载警告基线与 `cxx_std_20`）✅ 2026-09-20
- [x] 警告基线：`-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual`；`-Werror` 由 `YR_WARNINGS_AS_ERRORS` 控制（只在 CI 开）✅ 2026-09-20
- [x] `engine/core` target：`yr_core` + `yr::core` ALIAS + `include/yr/core/` 分层 include 约定 + `version.h/cpp`（把 CMake 的版本号与 `$<CONFIG>` 注入 C++）✅ 2026-09-20
- [x] Catch2 集成：`find_package(Catch2 3 QUIET)` + **FetchContent 回退**（本机走系统包 3.7.1，CI 走 FetchContent，因为 Ubuntu noble 的 apt 源没有 `libcatch2-dev`）+ `include(Catch)` + `catch_discover_tests`；`tests/core/test_smoke.cpp` ✅ 2026-09-20（回退路径 09-22 补）
- [x] `CMakePresets.json`：`debug` / `release` / `asan` 三套（configure + build + test），共享 hidden `base`，`binaryDir = build/${presetName}`，`outputOnFailure: true` ✅ 2026-09-22
- [x] Sanitizer 开关：`YR_ENABLE_SANITIZERS`（ASan + UBSan + `-fno-omit-frame-pointer` + `-fno-sanitize-recover=all`），与 debug 分离的理由见 **ADR D16** ✅ 2026-09-22
- [x] clangd 接入：根目录 `compile_commands.json` → `build/debug/` 符号链接；禁用 C/C++ 扩展 IntelliSense 避免冲突 ✅ 2026-09-22
- [x] **门禁全部实测**：`-Wshadow` 会响 / `-DYR_WARNINGS_AS_ERRORS=ON` 构建失败退出码 1 / ASan 抓到 `heap-buffer-overflow`（含分配点与越界字节数）/ UBSan 抓到 `signed integer overflow` / debug preset 下同一份代码零报告 / `clang-format --dry-run -Werror` 合规 ✅ 2026-09-22

- [x] `tools/check_deps.py`：include 反向依赖 + 禁用符号（裸 `new`/`delete`）+ 图形 API 泄漏扫描；已接进 CI lint job ✅ 2026-09-24
- [x] `.github/workflows/ci.yml`：`build-test` matrix（gcc-13 × clang-18）×（debug/release/asan）+ `lint` job（clang-format 18 + `check_deps.py`）；`fail-fast: false` ✅ 2026-09-24
- [x] **吸收 yo_lib ①**：`yr/core/assert.h` + `src/assert.cpp`。`YR_ASSERT` / `YR_ASSERT_MSG` / `YR_VERIFY` / `YR_BREAKPOINT`；可注入 `AssertHandler` 使失败路径可测；重入哨兵；`YR_ENABLE_ASSERTS` 由 CMake 统一下发 ✅ 2026-09-25
- [x] **吸收 yo_lib ②**：`yr/core/log.h` + `src/log.cpp`。`YR_LOG_DEBUG/INFO/WARN/ERROR/FATAL`（tag + 调用点信息）；编译期剔除 + 运行期 level 过滤；可注入 `LogHandler`；`YR_ENABLE_LOG` / `YR_ENABLE_DEBUG_LOG` 由 CMake 统一下发 ✅ 2026-09-26

**与原计划的偏差**（记下来，免得三个月后困惑）
| 计划里写的 | 实际 | 原因 |
|---|---|---|
| `cmake/YrLibrary.cmake`（`yr_add_library()` 封装） | **推迟到 M1/M2** | 只有 1 个 target 时抽象没有意义；等第 2、3 个 target 出现重复样板再抽（同 ADR D6 的思路） |
| `tsan` / `ci` preset | **推迟**（tsan→M7，ci→Day 5） | 决策协议 R3：不痛就不做 |
| ccache | **推迟到 Day 5** | 它是"提速"不是"能力"；做 CI 缓存时才知道省了多少 |
| `.editorconfig` | 未建 | clang-format 已覆盖格式；需要时再加 |
| `tests/core/test_main.cpp` | 实际叫 `test_smoke.cpp` | 用 `Catch2::Catch2WithMain`，不需要自定义 main（M2 注册反射类型时才需要换） |
| sanitizer 做成 `cmake/YrSanitizers.cmake`（列表解析 + 互斥检查） | **5 行 `option()` 写在顶层** | TSan 到 M7 才用；避免引入 `foreach`/`list(FIND)`/`FATAL_ERROR` 三组新语法 |
| log 的 fmt 风格格式化 | **推迟**（架构已就位） | 格式化是叶子功能，不改接口形状；先确认 `std::format` 可用性再定方案，别手写解析器 |

### 剩余（Day 7 收口）

- [ ] `LICENSE`（MIT）—— 仓库已 public，缺 LICENSE 等于"保留所有权利"，别人无法合法引用
- [ ] 装 ccache 并接进 CMake（`find_program` 探测，装了就用、没装不报错）
- [ ] 根 `README.md` 加 CI 徽章（workflow 已跑），里程碑表 M0 → ✅ 留到 M0 全部收口时再改
- [ ] log 补完（推迟，非阻塞）：
  - fmt 风格**格式化引擎**（`{}` 占位符）。当前宏只传格式串本身，`__VA_ARGS__` 不求值；`test_log.cpp` 的 `[wip]` 用例守着这条
  - 输出补 时间戳 / 帧号 / 线程 ID（`05-engineering.md` §5）
  - 线程安全（单 mutex + 行缓冲）与 `InlineBuffer` SBO 优化 → 与 M7 一起做
- [ ] 补 `git tag v0.M0`

### 验收标准

- `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` 三条命令全绿；冷启动 < 60s、热构建 < 5s（**实测：configure 0.15s / 全量 1.2s / 增量 0.18s** ✅）
- 故意在 `yr_core` 里 include 一个上层头文件 → `check_deps.py` 报错并让 CI 失败 ✅ 2026-09-24（已实测）
- 故意写一个 `new` → CI 失败 ✅ 2026-09-24（已实测）
- GitHub Actions 徽章出现在 README，且 matrix 全绿

**学习点**：CMake target vs 变量、generator expression、`target_*` 的 PUBLIC/PRIVATE/INTERFACE 传播、preset 的三种类型、ODR、`-Wshadow` 抓到的第一类真 bug、sanitizer 的插桩原理与代价。
**常见坑**：① `file(GLOB_RECURSE)` 新增文件不触发重新配置 → **本项目禁用 GLOB，显式列源文件**；② `include(Catch)` 漏写 → `catch_discover_tests` 未定义；③ `enable_testing()` 必须在 `add_subdirectory(tests)` 之前；④ sanitizer flag 编译与链接都要加，否则 `undefined reference to __asan_init`；⑤ `cmake --build --preset X` 不会自动 configure；⑥ GCC 的 `-Wshadow` 不抓"局部变量遮蔽函数名"，只抓变量遮蔽变量。
**Godot 对照**：`SConstruct` + `methods.py` + `core/SCsub`（看它如何组织"每目录一个构建脚本"），对照你的 CMake 分层。
**降级方案**：CI 只跑 gcc + debug；clang-tidy 推到 M1。

---

## M1 · 句柄、容器、时间（1~2 周）

**目标**：把"引用一个可能已死的对象"这件事一次性解决，并建立第一个性能测量习惯。

- [ ] `yr/core/handle.h`：`Handle<T>`（index+generation 打包、值语义、可哈希、`[[nodiscard]]`）
- [ ] `yr/core/slot_map.h`：`SlotMap<T>`（稠密数组 + 空闲链 + generation）
- [ ] `yr/core/sparse_set.h`：吸收 yo_lib `yo_sparse_set.h`，**重构**：去掉 `std::vector<int>` 的 -1 哨兵改用 `uint32_t npos`、加 `remove_unstable`/`remove_stable` 两种语义、加迭代器
- [ ] `yr/core/string_id.h`：`StringId` 字符串驻留（hash → index，`intern()` / `to_string()`，进程级表 + 启动后只读优化）
- [ ] `yr/core/time.h`：`Duration`/`TimePoint`（`std::chrono` 别名）+ `Clock`（对照 §4.7）
- [ ] `yr/core/object_id.h`：`ObjectID`（全局唯一 64-bit）——只定义类型与 `ObjectDB` 接口，实现留到 M2
- [ ] `yr/core/math.h`：最小 `Vec2/Vec3/Vec4/Mat4/Transform3D/Quaternion`（决策 Q1；先做 Vec3 + Mat4 + Transform3D，其余按需）
- [ ] 测试：Handle 失效检测、generation 回绕、SlotMap 100 万次插删后无碎片、StringId 驻留一致性
- [ ] **benchmark**：`benchmarks/bench_slot_map.cpp`（SlotMap vs `unordered_map` vs `vector+标记位`，测插入/查找/遍历三项）
- [ ] 用 `perf stat` 记录 cache-miss 差异，写进 benchmark 报告

**验收标准**
- `Handle<T>` 与 `Handle<U>` 不能隐式互转（编译期测试 `static_assert(!std::is_convertible_v<...>)`）。
- SlotMap：erase 后旧 handle 的 `get()` 返回 `nullptr`；index 被复用后旧 handle 依然失效（**这是 ABA 测试，必须有**）。
- benchmark 报告：遍历 SlotMap 比 `unordered_map<uint32_t, T>` 快 ≥3x（若没有，分析原因并写下来——分析比数字重要）。
- ASan preset 下全部测试干净。

**产出物**：`benchmarks/` 下的 benchmark 代码 + `benchmarks/README.md` 里的数据表与结论（方法必须可复现：硬件、编译选项、迭代次数、如何防止被优化掉）。
**学习点**：位打包、稠密/稀疏数组、cache line、false sharing 初体验、`std::chrono`、benchmark 方法论（预热、多次取中位数、防编译器优化掉）。
**常见坑**：① generation 溢出回绕导致 ABA（40 bit 够用，但要写测试证明你想过）；② `SlotMap::get` 返回 `T*` 后容器扩容导致指针失效（文档写明"指针只在本帧有效"）；③ StringId 的哈希冲突（用"hash 定位 + 字符串比较确认"，不要只信 hash）。
**Godot 对照**：`core/templates/rid.h` + `rid_owner.h`（generation + slot）、`core/object/object_id.h`、`core/string/string_name.h`（StringName 的实现，含 512 个 slot 的 hash 表）。
**降级方案**：`SparseSet` 推到 M4（Transform SoA 需要时再写）；数学库只做 `Vec3/Mat4`。

---

## M2 · 反射与对象模型（2~3 周）★ 项目分水岭

**目标**：让 C++ 类型在运行时可被"看见"。这是序列化、检视器、事件、脚本的共同地基，也是本项目最有含金量的模块。

- [ ] `yr/object/variant.h`：Variant（第一版 9 种类型，见 §4.3）+ 完整测试（类型混淆、拷贝、比较、Dict 保序）
- [ ] `yr/object/object.h`：`Object` 基类 + `notification(uint32_t)` + `ObjectID`
- [ ] `yr/object/object_db.h`：`ObjectDB`（`ObjectID → Object*`，注册/注销/查询，Debug 下统计存活数与类型分布）
- [ ] `yr/object/ref_counted.h`：`RefCounted` + `Ref<T>` + `WeakRef<T>`（走 ObjectDB）
- [ ] `yr/object/property_info.h` + `class_info.h`：`PropertyInfo`（name/type/flags/default/hint）+ `ClassInfo`（属性表 + factory + get/set 适配 + 继承链）
- [ ] `yr/object/class_db.h`：注册表 + `instantiate(StringId)` + `inheritors_of` + `freeze()`
- [ ] **注册宏** `YR_CLASS` / `YR_PROPERTY`：先手写一个不用宏的版本（直接构造 `ClassInfo`），跑通后再封装成宏
- [ ] 用 `clang -E` 展开一次 `YR_CLASS`，读懂生成的每一行（**这是唯一能真正看懂宏的办法**）
- [ ] `tools/yr_inspect`：命令行反射查看器（`--all` / `--class Node` / `--tree`），支持输出 JSON
- [ ] 测试：注册→实例化→按名字 get/set→未注册类报错→继承链属性可见→`freeze()` 后注册报错

**验收标准**
- 新增一个类只需 3 行宏 + 成员声明，不需要改任何中心文件（**这是反射系统是否设计正确的硬指标**）。
- `yr_inspect --all` 输出所有已注册类及其继承关系与属性表。
- 通过反射读写属性与直接访问成员的结果完全一致（测试覆盖 int/float/string/vec3/objectid/array/dict）。
- `ObjectDB` 在测试结束时存活对象数为 0（泄漏检测）；ASan 干净。
- 静态初始化顺序问题：显式 `register_core_classes()` 调用，`main` 之前不依赖任何注册（写一个测试证明）。

**产出物**：`yr_inspect` 工具（可截图/录屏，是很好的展示物）。宏展开的分析结论写进 `YR_CLASS` 的头文件注释。
**学习点**：宏工程（`__VA_ARGS__` / `__VA_OPT__` / token pasting / 静态注册器技巧）、成员指针、类型擦除、lambda→函数指针、`if constexpr`、concepts、SIOF、侵入式引用计数。
**常见坑**（这个模块的坑最多，逐个记录到笔记）：
① 宏里用 `decltype(member)` 推导失败 → 需要 `Type::*` 成员指针而非直接取地址；
② 静态注册器在动态库里被链接器丢弃（`--whole-archive` 问题）→ 本项目全静态库，但要知道这个坑；
③ `ClassInfo` 里的 lambda 捕获导致不能转函数指针 → 用无捕获 lambda 或 `void*` 成员偏移；
④ Variant 的 `Dict`/`Array` 深拷贝性能 → 先不优化，测出来再优化；
⑤ 属性默认值存储：放 `ClassInfo` 里（每类一份）而不是每对象一份。
**Godot 对照**：`core/object/object.h`（`GDCLASS` 宏、`_bind_methods`、`notification`）、`core/object/class_db.h/.cpp`（`bind_property` / `ClassInfo` 结构 / `instantiate`）、`core/object/property_info.h`、`core/object/method_bind.h`（方法绑定的类型擦除，比属性绑定更难，选读）、`core/register_core_types.cpp`（显式注册顺序）、`tests/core/object/test_class_db.cpp` + `test_object.cpp`（**看成熟项目怎么测反射**）。
**降级方案**：只做 `YR_CLASS` + `YR_PROPERTY`，不做方法绑定（`YR_BIND_METHOD` 推到 stretch）；Variant 只做 6 种类型（null/int/float/string/vec3/objectid）。

---

## M3 · 事件系统与延迟调用（1~2 周）

**目标**：让模块之间不需要互相 include 就能通信，并解决"回调里改容器"的经典难题。

- [ ] `yr/event/subscription.h`：强类型 `Subscription`（RAII，析构自动退订）
- [ ] `yr/event/event_bus.h`：吸收 yo_lib `yo_eventsys.h` 并按 §4.5 重构（删 `lastMsg_`、强类型 token、`publish`/`post` 分离、双缓冲队列）
- [ ] `yr/event/message_queue.h`：`MessageQueue`（deferred call：`ObjectID` + `StringId` + `vector<Variant>`；flush 期间新增进下一帧）
- [ ] 重入策略实现 + assert：发布深度上限、退订自己安全、flush 双缓冲
- [ ] 线程约束：`EventBus::publish` 加主线程 assert；提供 `post_from_any_thread`（MPSC，帧首合并）
- [ ] 把 `Object::notification()` 与 EventBus 的分工想清楚（notification = 定向、沿继承链传播；event = 广播、跨模块），结论写进两者的头文件注释
- [ ] 测试：§4.5 表格里那三条语义各一个用例；1000 事件/帧的压力测试；订阅者抛异常/退订其他订阅者的边界测试
- [ ] 事件追踪：`YR_LOG_DEBUG` 打印每次 publish 的类型与订阅者数量（调试期极其有用）

**验收标准**
- §4.5 三条语义测试全绿（**这三条是模块的真正交付物**）。
- `Subscription` 析构后不再收到事件（RAII 测试）。
- 同一帧内事件派发顺序确定（同样输入两次运行日志逐字节相同）——这是"可复现"的基础，写进测试。
- TSan preset 下 `post_from_any_thread` + 主线程 flush 无竞争报告。

**产出物**：一个"确定性重放"测试（同输入两次运行日志逐字节相同）。
**学习点**：`std::function` 的堆分配与小对象优化、变参模板、MPSC 队列、重入与迭代器失效、RAII 句柄。
**常见坑**：① `std::function` 捕获大对象导致每次订阅都堆分配（测一下，决定是否用 `unique_function` + 移动）；② 事件类型用 `typeid` 做 key 的 RTTI 开销与跨 TU 一致性（yo_lib 用 `type_index`，可以保留但要理解代价；更好的是用 §4.5 的 `kTypeId` 静态常量）；③ flush 中 post 导致死循环。
**Godot 对照**：`core/object/object.h` 的 signal（`connect`/`emit_signal`/`Callable`）、`core/object/message_queue.h/.cpp`（**精读**：双缓冲、flush 时机、`push_call`）、`core/object/callable_mp.h`（成员函数指针 → Callable 的擦除手法，与 M2 的属性适配同源）。
**降级方案**：不做 `post_from_any_thread`（推到 M7）；`MessageQueue` 只支持无参调用。

---

## M4 · 场景树与主循环（3~4 周）★ 运行时的灵魂

**目标**：**第一次有一个"活着的世界"**。这个里程碑结束时，你应该能在终端看到一个世界 tick 一万帧，节点在创建、更新、销毁。

- [ ] `yr/scene/node_path.h`：`NodePath`（`"Root/Level/Player"` 解析、相对路径 `../`）
- [ ] `yr/scene/node.h/.cpp`：`Node`（父子关系、`add_child`/`remove_child`、`queue_free`、`is_inside_tree`、`get_node(path)`、名字唯一性）
- [ ] 生命周期通知：`kEnterTree` / `kReady`（后序）/ `kProcess`（前序）/ `kPhysicsProcess` / `kExitTree`，顺序写进文档并用测试锁定
- [ ] `yr/scene/scene_tree.h/.cpp`：`SceneTree`（拥有 root、遍历派发、待处理队列、`flush_deletions`、`SceneTreeTimer`）
- [ ] Transform 层级：`local_transform` + `global_transform`，脏标记 + 向下传播；**平行 SoA 数组**（为 M7 并行与 M9 快照抽取铺路）
- [ ] `yr/engine/main_loop.h/.cpp`：按 §5 的 14 个阶段实现（本阶段只填 0/2/3/5/7/9/13，其余留空钩子）
- [ ] 固定步长累加器 + spiral-of-death 保护（单帧最多 N 次 physics step）+ `time_scale` + `paused`
- [ ] `yr/engine/engine.h/.cpp`：`Engine`（装配、显式启停顺序、`FrameStats`）
- [ ] `yr/engine/frame_stats.h`：每阶段耗时（`Clock` 采样）+ 每 N 帧打印 + 可导出 CSV
- [ ] headless 平台层：`yr/platform/headless.h`（无窗口、输入从 stdin/脚本注入、可 `--frames N` 后自动退出）——**这让 CI 能跑集成测试**
- [ ] `apps/tick_sandbox`：一个 demo，脚本化生成/销毁节点、打印树、跑 N 帧（**M4 的"可运行物"**）
- [ ] 集成测试：跑 10000 帧 → 断言节点数、事件数、无泄漏、ObjectDB 清空

**验收标准**
- **生命周期顺序测试**：嵌套 3 层树，`_ready` 后序 / `_process` 前序 / `_exit_tree` 顺序，逐条断言。
- **删除安全测试**：节点在自己的 `kProcess` 里 `queue_free()` 自己 / 父节点 / 子节点 / 兄弟节点，四种情况都不崩溃（ASan 干净）。这是本里程碑的核心价值。
- **遍历中新增**：`kProcess` 里 `add_child` 的新节点本帧不 process（与 Godot 一致），测试锁定。
- 10000 帧 headless 运行：ASan+UBSan 干净、ObjectDB 归零、帧耗时标准差合理（打印 p50/p95/max）。
- 固定步长：把 `real_delta` 人为抖动（模拟卡顿），physics 步数依然正确（写测试）。

**产出物**：`apps/tick_sandbox` 录屏（终端动画）+ `FrameStats` CSV。帧阶段顺序的定义写在 `01-architecture.md` §5，代码里的注释指向它。
**学习点**：组合模式、树遍历与迭代器失效、脏标记、通知模式、固定步长积分、性能采样、RAII 与延迟销毁。
**常见坑**：① `add_child` 时忘记设置 parent / 忘记派发 `kEnterTree` 到整棵子树；② `queue_free` 的 double free（父节点删了，子节点也在待删列表）；③ `get_node(path)` 每次都做字符串解析（缓存 `NodePath` 解析结果）；④ Transform 脏标记向上还是向下传播搞反；⑤ 暂停时 `SceneTreeTimer` 是否继续走（Godot 有 `process_always`/`process_in_physics` 标志，想清楚再实现）。
**Godot 对照**：`scene/main/node.h` + `node.cpp`（`_propagate_ready`/`_propagate_enter_tree`/`_propagate_exit_tree`/`queue_free`）、`scene/main/scene_tree.h/.cpp`（`process`/`physics_process`/`flush_transform_notifications`）、`scene/main/scene_tree.h:57`（`SceneTreeTimer`）、`main/main.cpp`（`Main::iteration()` —— **一帧的权威顺序，M4 必读**）、`main/main.h`。
**降级方案**：Transform 层级只做 position（不做旋转缩放）；`SceneTreeTimer` 推到 M5；`FrameStats` 只做总耗时。

---

## M5 · 序列化与场景资源（3~4 周）★ 最难讲清也最出彩

**目标**：世界可以变成文件，文件可以变回世界，且**文件是人类可读可手写的**。

- [ ] `yr/serialize/writer.h` / `reader.h`：`IWriter`/`IReader` 抽象（结构化 begin/end + 错误位置报告）
- [ ] `TextWriter`/`TextReader`：自定义 `.yrscn`。**先把格式写下来再写解析器**（在对话里过一遍：段结构、引用怎么表达、错误怎么报），格式定稿后写进 `yr/serialize/` 的头注释
- [ ] `BinaryWriter`/`BinaryReader`：`.yrscnb`（magic + version + 长度前缀；处理对齐与字节序，写 `static_assert(sizeof(...))`）
- [ ] `JsonWriter`：调试导出（用 nlohmann_json，M5 起才引入 third_party）
- [ ] `VariantCodec`：Variant ↔ 文本/二进制（含 Vector3 等类型的字面量语法）
- [ ] 属性级序列化：遍历 `ClassInfo` 属性表，只序列化 `kSerialized` 且**与默认值不同**的属性（省空间 + diff 干净）
- [ ] 引用解析：ExtResource（外部资源，存 path/UID）/ SubResource（内嵌子资源）/ NodeRef（同场景节点，存 node index）
- [ ] `yr/scene/packed_scene.h`：`PackedScene`（场景的内存中间表示：节点数据表 + 父子关系 + 连接），`instantiate()` 生成真实 Node 树
- [ ] 版本兼容：`format_version` + 未知属性 warning 跳过 + 缺失属性用默认值 + migrator 链（写一个 v0→v1 的假迁移做测试）
- [ ] `tools/yr_scene_conv`：文本 ↔ 二进制互转 + `--validate`
- [ ] 测试：round-trip（save→load→save，两次输出**逐字节相同**）、手写文件加载、未知属性容忍、破坏性变更迁移、循环引用、深层嵌套
- [ ] 把 M4 的 `tick_sandbox` 场景改成从 `.yrscn` 加载

**验收标准**
- **确定性 round-trip**：`yr_scene_conv a.yrscn → a.yrscnb → b.yrscn`，`diff a.yrscn b.yrscn` 为空。（这条最难，也最能证明设计正确）
- 手写一个 20 行的 `.yrscn`（不经工具生成）能被正确加载。
- 加载一个含未知属性/未知类的文件 → 不崩溃，warnings 列表准确指出行号与内容。
- v0 文件经 migrator 后能被当前版本加载（测试）。
- 序列化 1000 节点树的耗时与文件大小记录进 benchmark 报告。

**产出物**：`yr_scene_conv` + round-trip 测试 + **格式规范**（写在 `yr/serialize/` 的头文件注释或 `assets/README.md` 里，不单独建文档）。**这是面试里最容易讲出深度的模块**（引用/版本/循环三大难题）。
**学习点**：格式设计、visitor 模式、token 解析（手写 lexer/parser，不用 lex/yacc）、二进制布局与对齐、版本迁移策略、确定性输出（保序容器、浮点格式化）。
**常见坑**：① 浮点数文本化精度丢失（用 `%.17g` 或直接存十六进制位模式）；② `Dict` 用 `unordered_map` 导致输出顺序不定 → §4.3 已规定用保序 vector；③ 节点 index 与父子关系混在一起导致解析要两遍（就是要两遍，别抗拒）；④ 字符串转义（`\n`、引号、UTF-8）；⑤ 文件路径平台分隔符。
**Godot 对照**：`scene/resources/resource_format_text.cpp/.h`（`.tscn` 的完整读写实现，**M5 精读，对照你的格式设计**）、`core/io/resource_format_binary.cpp`（`.res` 二进制 + 版本兼容处理）、`scene/resources/packed_scene.h/.cpp`（`PackedScene::instantiate` 的 node data / connection / edit state 三张表）、`tests/core/io/test_scenes.cpp`（Godot 怎么测场景存读档）、`core/variant/variant_parser.h`。
**降级方案**：二进制格式推到 M11（先只做文本）；`PackedScene` 先做扁平版（不支持内嵌子资源与场景嵌套实例化）；migrator 只留接口不实现链。

---

## M6 · 资源系统（2~3 周）

**目标**：资源有身份（UID）、有注册表、有引用计数、能异步加载不卡帧、能自动卸载。

- [ ] `yr/asset/asset_uid.h`：`AssetUID`（内容哈希或稳定 ID）+ `path ↔ uid` 映射表（对照 Godot `resource_uid.h`）
- [ ] `yr/asset/resource.h`：`Resource : RefCounted`（source_path / uid / 加载状态 / 元数据）
- [ ] `yr/asset/file_access.h`：`IFileAccess` 抽象 + `DiskFileAccess` + **`MemoryFileAccess`（测试用，见原则 P5）**
- [ ] `yr/asset/format_loader.h`：`IResourceFormatLoader` + 按扩展名分派注册表；实现 `.yrres`（M5 的二进制/文本资源）、`.json`、`.txt`（M8 剧情用）
- [ ] `yr/asset/asset_database.h/.cpp`：`AssetDatabase`（同步 `load<T>`、异步 `request_load`、in-flight 合并、`pump()`、`collect_garbage()`、`AssetStats`）
- [ ] 异步加载：**先做单线程版**（`request_load` 只是入队，`pump()` 里同步执行），M7 再换成 job；这个"错的但能跑的"版本要记进 ADR
- [ ] 慢 IO 模拟：`ThrottledFileAccess`（每次读 sleep N ms），用来**证明异步加载不卡帧**（这是本里程碑的核心测试）
- [ ] 卸载：帧末 `collect_garbage()`（refcount==1 即只有 DB 持有）+ 手动 `evict(uid)` + 统计
- [ ] 接到 `MainLoop` 阶段 4（`pump_assets`）与阶段 12（`collect_garbage`）
- [ ] 把 M5 的 ExtResource 解析接到 AssetDatabase（场景加载 = 递归资源加载，**注意加载顺序与循环依赖检测**）
- [ ] 测试：in-flight 合并（同一资源并发请求 10 次只加载一次）、失败路径（文件不存在/格式错误）、卸载后 handle 失效、慢 IO 下主循环帧时间不受影响

**验收标准**
- 用 `ThrottledFileAccess`（单次读 50ms）连续请求 20 个资源：主循环帧时间 p95 < 5ms，20 个资源在 ~1s 内全部就绪（异步生效）。
- 同一资源被 5 处引用 → 内存里只有一份（测试用 ObjectDB 计数验证）。
- 全部引用释放后下一帧 GC → 资源被卸载，`resident_count` 归零。
- 循环依赖（A 引用 B，B 引用 A）→ 检测到并报错，不死循环。
- 所有测试用 `MemoryFileAccess`，**不碰真实磁盘**（可在 CI 跑）。

**产出物**：`AssetStats` 输出（驻留数/命中率/加载字节）。
**学习点**：异步状态机、future/promise vs 回调、引用计数与 GC 的边界、内容寻址、IO 抽象与依赖注入（测试友好设计）。
**常见坑**：① 在工作线程构造 Object → 违反 §6 铁律（M7 要特别小心）；② GC 时机与 `Ref` 临时对象（一个表达式里的临时 `Ref` 会让 refcount 短暂 >1，导致该帧不卸载 —— 这是正常行为，别"修"它）；③ 加载失败后 in-flight 条目没清理 → 永久卡住；④ 场景递归加载的深度与栈溢出。
**Godot 对照**：`core/io/resource.h`、`core/io/resource_loader.h/.cpp`（**精读 `ThreadLoadTask` 结构与 `load_threaded_request`/`load_threaded_get_status`，这是 M6+M7 的最佳参考**）、`core/io/resource_uid.h/.cpp`、`core/io/file_access.h` + `file_access_memory.h`（内存 FS）、`file_access_pack.h`（PCK，M11 用）、`tests/core/io/test_resource.cpp` + `test_resource_uid.cpp`。
**降级方案**：不做 UID（只用 path）；GC 只做手动 `evict`；异步只做"入队 + pump 执行"不做真线程。

---

## M7 · 任务系统（3~4 周）★★ 最容易翻车，务必按三步走

**目标**：把 M6 的"假异步"变成真并行，并让 Transform 更新、资源解码、（M9 的）剔除能跑在多核上——**同时不引入不可复现的 bug**。

- [ ] **M7-a 线程池**：`yr/job/job_system.h`：N 个 worker + `MtQueue`（吸收 yo_lib `yo_mtqueue.h` 并重构：加 `try_pop`、`stop()` 幂等、避免 busy wait）+ `submit` + `wait_all` + 优雅停机
- [ ] `TaskHandle`：`ready()` / `wait()`；主线程 `wait` 时**帮忙执行任务**而不是 sleep（对照 Godot）
- [ ] 把 `AssetDatabase` 的 IO+解码搬到 worker，构造仍回主线程（`pump()`）——**验证 §6 铁律**
- [ ] **M7-b 依赖图**：`submit(fn, deps)`（依赖计数 + 就绪队列）；`parallel_for(n, batch, fn)`
- [ ] Transform 层级并行刷新：按"脏根"分片，保证同一子树不并行（写清楚分片策略）
- [ ] **M7-c（可选）work stealing**：每线程本地双端队列 + 随机偷取；此时才深入 memory order
- [ ] `JobStats`：每线程独立 cacheline（`alignas(64)`）避免 false sharing；统计队列深度/等待/窃取次数
- [ ] **确定性保证**：并行任务的**结果**必须与串行一致（浮点求和顺序！`parallel_for` 里禁止做非结合性归约，或提供确定性归约接口）——写测试证明
- [ ] 线程安全审计：给所有"仅主线程"的 API 加 `YR_ASSERT(on_main_thread())`
- [ ] **内存序决策写进 `yr/job/` 的头文件注释**：哪个 atomic 用哪个 order、为什么。这类信息离开代码就会失效，不适合放文档
- [ ] benchmark：`benchmarks/bench_jobs.cpp`（1k/10k/100k 个任务的串行 vs 并行；不同 batch size 的曲线）
- [ ] TSan preset 跑全部测试 + `tick_sandbox` 10000 帧

**验收标准**
- TSan 下全部测试 + 10000 帧集成测试**零报告**（这一条不过就不算完成）。
- `parallel_for` 结果与串行逐元素相同（含浮点，测试用固定种子数据）。
- benchmark：10k 个 100µs 任务，8 线程加速比 ≥5x（达不到要分析：任务粒度？队列锁？false sharing？分析写进报告）。
- 优雅停机：`~JobSystem()` 在有待处理任务时不死锁、不泄漏线程（ASan+TSan 干净）。
- 关闭 job（`--jobs 0`）后所有测试依然通过 —— **单线程路径必须始终可用**，这是可调试性的保命设计。

**产出物**：benchmark 报告 + 内存序决策表 + TSan 报告截图。
**学习点**：C++ 内存模型、`memory_order_relaxed/acquire/release/acq_rel/seq_cst` 的实际差别、false sharing、work stealing、任务粒度、`std::atomic_flag`/futex、条件变量的 lost wakeup。
**常见坑**：① 条件变量 `wait` 忘 predicate → lost wakeup 死锁；② `wait_all` 在 worker 线程调用 → 死锁（必须 assert）；③ 任务里捕获 `this` 而对象已析构 → 用 handle + 生存期保证；④ 析构顺序：JobSystem 必须在 AssetDatabase 之后销毁（否则任务回调访问死对象）—— 写进 `Engine::shutdown` 的顺序文档；⑤ 浮点归约顺序导致不可复现；⑥ 过度并行（任务太小，锁开销 > 收益）。
**Godot 对照**：`core/object/worker_thread_pool.h/.cpp`（**精读**：task group、`wait_for_task_completion`、主线程帮忙执行的逻辑）、`core/templates/command_queue_mt.h`（跨线程命令队列）、`core/io/resource_loader.cpp` 的 threaded load 部分（真实项目里"IO 并行 + 主线程收尾"的完整范例）、`servers/server_wrap_mt_common.h`（M10 会用）。
**降级方案**：只做 M7-a（线程池 + `wait_all`）；`parallel_for` 用 `std::async` 顶替；work stealing 直接砍掉列入 stretch。**M7 砍范围是完全可接受的**，确定性比性能重要。

---

## M8 · 文字冒险 Demo（2~3 周）★ 作品节点 1

**目标**：**证明 Runtime 真的能承载游戏**。这个 Demo 是本项目第一个可以拿去给别人看的东西，也是检验前 7 个里程碑的试金石——如果做起来处处别扭，说明 Runtime 设计有问题，**要回头改 Runtime，而不是在 Demo 里绕过去**。

- [ ] 游戏设计先想清楚再动手：世界观、房间图、动词表（look/go/take/use/talk/inventory）、胜利条件、≥10 分钟内容量（写在 `apps/text_adventure/README.md` 里）
- [ ] 数据驱动：房间/物品/对话全部放 `assets/`（`.yrscn` 场景 + `.json`/`.yrdlg` 文本），**代码里不写死内容**
- [ ] `apps/text_adventure`：用 Node 组织世界（`World/Rooms/Room1/...`、`Player`、`Inventory`、`DialogueRunner`、`CommandParser`、`SaveGameManager`）
- [ ] 命令解析：tokenizer + 动词/名词匹配 + 同义词表 + 错误提示（**用 EventBus 解耦**：`CommandParsed` → 各系统响应）
- [ ] 交互系统：`Interactive` 组件 + `on_use` 指向 NodePath（**用 M5 的 NodeRef 序列化**）
- [ ] 对话系统：`Dialogue` 资源（异步加载，**用 M6**）+ 分支 + 打字机效果（**用 M4 的 SceneTreeTimer / kProcess**）
- [ ] 存档/读档：整个 `World` 子树 → `.yrscn`（**用 M5**），读档时资源引用正确复原（**用 M6**）
- [ ] 触发器/脚本化事件：进入房间触发剧情（`AreaTrigger` 节点 + 事件），定时事件（`SceneTreeTimer`）
- [ ] headless 自动化游玩测试：一个"机器人脚本"执行 200 条命令通关 → CI 里跑（**这是最有说服力的集成测试**）
- [ ] 终端体验：ANSI 颜色、房间描述、`help`、`--seed`、命令历史
- [ ] README（apps/text_adventure/）：截图 + 玩法 + 用到的 Runtime 特性对照表
- [ ] 录屏（asciinema 或 mp4）放进 `doc/media/`

**验收标准**
- 从零开始可玩通 ≥10 分钟内容并达成结局；存档→重启→读档后状态一致（含背包、已触发剧情、房间状态）。
- **游戏逻辑代码零直接 Runtime 内部访问**：只用 `Node`/`EventBus`/`AssetDatabase`/`SceneTree` 的 public API（证明 API 设计够用）。
- CI 里自动通关测试通过（headless，无 tty 依赖）。
- 新增一个房间 + 一件物品**只需改 assets/ 文件，不需要改 C++ 代码**（数据驱动的硬指标）。
- ASan+UBSan 干净跑完整局。

**产出物**：**可玩 Demo + 录屏 + README + 自动通关测试**。这是简历上第一条能写的东西。
**学习点**：用引擎做游戏（视角切换：从"造引擎"到"用引擎"，会暴露 API 设计问题）、数据驱动设计、内容组织、玩法迭代。
**常见坑**：① 内容量低估（10 分钟内容 ≈ 15 个房间 + 30 件物品 + 500 行对话，写内容比写代码慢）；② 为了赶 Demo 在 Node 里塞硬编码逻辑 → 违反数据驱动，坚决不加；③ 存档包含运行时状态（如 timer 剩余时间）导致语义混乱 → 明确"哪些是可序列化的游戏状态"；④ 命令解析用大量 if-else → 用表驱动。
**Godot 对照**：`modules/`（Godot 的模块如何注册类型与场景）、随便找一个 Godot 开源文字冒险 demo 看它怎么组织场景树（对照你的 Node 划分）。
**降级方案**：内容量砍到 5 分钟 / 5 个房间；对话系统砍分支只留线性；触发器砍掉只做手动命令。**但存档/读档和数据驱动不能砍**——那才是 Runtime 的展示点。

---

## M9 · 渲染契约与 NullBackend（2 周）

**目标**：定义 Runtime 与 Renderer 之间那条线，并用一个什么都不画的后端验证它。

- [ ] `yr/render/snapshot.h`：`RenderSnapshot` 及全部 POD 类型（§4.11），**每个字段的单位/坐标系/矩阵序都写注释**
- [ ] `yr/render/backend.h`：`RendererBackend` 接口 + `BackendConfig` + `BackendStats`
- [ ] `yr/render/asset_handle.h`：`AssetHandle`（Runtime 的资源 UID → Backend 的 GPU 资源 ID 的映射键）
- [ ] `yr/engine/render_extract.h/.cpp`：`RenderExtractSystem`（遍历 SceneTree → 可见性 → 生成 DrawItem/Camera/Light；M9 先做全量，M10 加视锥剔除）
- [ ] 排序键：`sort_key = (pipeline << 48) | (material << 24) | depth`（对照 Yo_Renderer roadmap §2.1）
- [ ] `yr_render_null`：`NullBackend`（统计 draw call / 三角形数 / 快照字节数，校验快照合法性）
- [ ] 双缓冲：快照写入 buffer[i]，Backend 读 buffer[i^1]，frame index 单调递增
- [ ] **快照重放**：`--dump-snapshot f.bin` 把快照写文件，`--replay-snapshot f.bin` 直接喂给 Backend（**验证契约完备性的杀手级测试**）
- [ ] 把 `01-architecture.md` §7 的三个解耦层次逐条对应到代码：哪一行实现了**数据解耦**、哪一行是**生命周期解耦**、哪一行是**时间解耦**（写在 `yr/render/snapshot.h` 的头注释里）
- [ ] CI 检查：`grep` 确保 `engine/`（除 `render/vulkan/`）零 `vulkan`/`GLFW` include
- [ ] 测试：快照 dump→replay 逐字节一致；NullBackend 在 10000 帧集成测试里统计数字稳定

**验收标准**
- headless + NullBackend 跑 M8 的文字游戏 1000 帧，快照统计正常，CI 全绿。
- 快照 dump/replay round-trip 一致。
- `yr_render_iface` 的头文件**只 include `yr/core/*` 与 std**（CI 检查 include 列表）。
- 能回答："如果换一个 OpenGL 后端，需要改 Runtime 的哪一行代码？"（答案必须是 0 行）

**产出物**：快照重放 demo。**这个主题面试高频，值得写成博客**（见 `07-portfolio.md` §4）。
**学习点**：数据契约设计、POD 与 ABI、双缓冲与 frame-in-flight、Server 模式、可重放性作为设计验证手段。
**常见坑**：① 快照里偷偷放了 `Object*`（"就这一次"）→ 解耦崩塌，CI 必须挡住；② 矩阵行列主序/坐标系（Y-up vs Z-up）没写清 → M10 画面全错且极难查，**现在就把注释写死**；③ 资源上传请求的时序（Backend 还没加载完 mesh，DrawItem 已经引用它）→ 契约里要规定"未就绪的资源本帧跳过"。
**Godot 对照**：`servers/rendering/rendering_server.h`（Server 命令接口）、`servers/server_wrap_mt_common.h`（**精读**：如何把一个 Server 自动包装成多线程版本）、`core/templates/command_queue_mt.h`。
**降级方案**：不做双缓冲（单缓冲 + 同线程）；不做快照重放（只做 dump 用于人工检查）。**但"Runtime 侧零 vulkan include"不能降级。**

---

## M10 · Vulkan 后端接入（4~6 周）★★ 作品节点 2

**目标**：把 Yo_Renderer 的成果接进来，形成 `游戏状态 → Runtime → Renderer → GPU` 的完整链路，并**顺手解决 Yo_Renderer 遗留的反向依赖问题**。

- [ ] 前置阅读：重读 `Yo_Renderer/doc/modularization-plan.md`（V1~V12 清单）与 `learning-roadmap.md` §1.3
- [ ] 搬迁策略决定并写 ADR：哪些目录搬（`platform/window`、`rhi/*`、`render/*`、`asset/gltf`），哪些**不搬**（`framework/*`、`system/*`、`ui/*` —— 这些被 Runtime 取代）
- [ ] `yr_render_vulkan` target：GLFW 窗口 + VulkanContext + Swapchain（直接搬 `platform/`）
- [ ] RHI 层搬入（`ResourceFactory`/`PipelineCache`/`DescriptorAllocator`/`UploadSession`/`ShaderLibrary`/`CommandRecorder`），**保持原样先跑通**，重构留到后面
- [ ] `VulkanBackend : RendererBackend`：`present(snapshot)` → 把 DrawItem 转成原来的 BatchSystem 输出 → RenderServer 录制
- [ ] **资源桥接**：`AssetHandle`（Runtime UID）→ GPU 资源；Backend 侧维护 `uploads` 队列 + generation 延迟销毁（复用 `VulkanResourceTable`）
- [ ] 相机/光照从快照取，删掉 Backend 内部对"场景"的假设
- [ ] `apps/render_demo`：Runtime 里搭一个场景（几个带 Transform 的 Node + Camera Node + Light Node），键盘控制 Node → 画面变化
- [ ] 视锥剔除放进 `RenderExtractSystem`（可在 job 里并行，**用 M7**）
- [ ] 修 Yo_Renderer 遗留问题：V5（光照布局三份副本 → 单一 `GpuLayouts.h`）、V6（`const void*` → 快照，天然解决）
- [ ] 独立渲染线程（stretch，见下）
- [ ] 搬迁时遇到的**耦合点逐条记进 commit message 正文**（这是唯一的高频记录载体，见 `START-HERE.md` §6）
- [ ] validation layer 零 error；`--dump-snapshot` + `--replay-snapshot` 在真后端也能跑
- [ ] 录屏 + 截图进 `doc/media/`，README 更新架构图

**验收标准**
- `apps/render_demo` 运行：键盘移动一个 Node → 画面同步变化；validation layer 零 error；连续跑 10 分钟无崩溃无泄漏。
- **`engine/` 目录（除 `render/vulkan/`）零 Vulkan/GLFW include**（CI 硬门禁）。
- 快照重放：把 M9 dump 的快照喂给 VulkanBackend，画面与实时运行一致。
- `RenderSnapshot` 结构自 M9 起**未因后端需要而修改**（若改了，说明契约设计有问题 → 写 ADR 复盘，这本身就是有价值的产出）。
- 帧时间统计：CPU 侧 extract / GPU 侧 present 分别可见（复用 Yo_Renderer 的 DebugStats）。

**Stretch（做完上面才考虑）**：独立渲染线程（Runtime 主线程写快照，渲染线程消费，对照 `server_wrap_mt_common.h`）；ImGui 只读检视器（显示 SceneTree + FrameStats）；阴影/IBL 接回。

**产出物**：**可视化 Demo + 录屏 + 更新后的架构图**。简历第二条。
**学习点**：图形 API 与引擎的边界、GPU 资源生命周期与延迟销毁、frame-in-flight、命令录制、（stretch）渲染线程同步。
**常见坑**：① 想"顺便重构 Yo_Renderer"导致范围爆炸 → **先原样搬通，再重构**，两件事分开 commit；② Backend 想回调 Runtime 拿数据（"就一个 getter"）→ 破坏解耦，改成快照字段；③ 资源加载与帧循环竞争（上传时机）；④ GLFW/窗口事件与 Runtime 输入系统的职责划分；⑤ 旧代码的 `ObjectDB`/`Ref` 与 YRuntime 版本冲突（同名不同类型）→ 搬入时统一改名到 `yr::render::vk` 命名空间。
**Godot 对照**：`servers/rendering/rendering_server_default.h`、`drivers/vulkan/`（Godot 的 Vulkan 驱动如何组织 RenderingDevice）、`servers/display/display_server_wrapper.h`。
**降级方案**：不做剔除、不做独立渲染线程、场景只用 3 个立方体 + 一个相机。**"Runtime 驱动画面"这一条不能降级**，它是本阶段的全部意义。

---

## M11 · 工具链与打磨（2~4 周，可与 M8~M10 并行）

**目标**：让项目从"一堆 C++ 代码"变成"有开发流程的引擎环境"，并完成作品包装。

- [ ] `tools/yr_pack`：`assets/` → `.yrpak`（manifest(JSON/二进制) + blob 区 + 内容哈希 + 版本号）；对照 Godot PCK
- [ ] `PakFileAccess`：Runtime 从 `.yrpak` 加载；`--pak` 启动参数；验证 M8 的游戏能完全从 pak 跑起来
- [ ] 资源导入管线雏形：`assets_src/`（源）→ `yr_pack` 转换/压缩 → `assets/`（cooked），写清"为什么需要 cook"
- [ ] 性能：接入 `perf` 工作流文档 + 可选 Tracy（submodule）；FrameStats 导出 CSV + 一个 Python 绘图脚本
- [ ] 调试文档：`.gdbinit`、常用断点配方、core dump 分析、ASan 报告解读、Catch2 `--break`
- [ ] clang-tidy：安装 + `.clang-tidy` 配置 + CI 接入（M0 遗留项）
- [ ] 覆盖率：gcovr/lcov → CI 上传报告，README 徽章
- [ ] 文档收尾：架构图更新（Mermaid）、每模块一页说明、`doc/README.md` 状态更新、README 重写（面向"第一次看到这个项目的人"）
- [ ] `07-portfolio.md` 修订并打勾：博客 ≥3 篇、录屏、benchmark 汇总、面试题库自测
- [ ] （可选）ImGui 只读检视器：SceneTree + 属性 + FrameStats
- [ ] （可选）热重载：`.yrscn` 文件变更 → 自动重载场景（inotify），演示效果极好

**验收标准**
- `apps/text_adventure --pak game.yrpak` 完全从 pak 启动运行，`assets/` 目录不存在也能跑。
- CI：build + ctest + format + clang-tidy + 覆盖率徽章全绿。
- 一个完全陌生的开发者按 README 能在 15 分钟内构建并跑起两个 Demo（**找人实测一次**）。
- `doc/` 与代码一致：架构图是最新的、`06-decisions.md` 收录了全部架构级取舍、`benchmarks/README.md` 有数据。

**产出物**：完整可展示仓库 + 博客系列 + 工具。
**Godot 对照**：`core/io/file_access_pack.h`（PCK 格式）、`editor/platform/`（导入管线）、`platform/web/`（打包思路）、`core/io/resource_format_binary.cpp`（cooked 格式）。
**降级方案**：Tracy、ImGui 检视器、热重载全部可砍；覆盖率只统计不做门禁。

---

## S · Stretch 清单（想法先进这里，不进当前里程碑）

| # | 想法 | 依赖 | 学习价值 | 备注 |
|---|---|---|---|---|
| S1 | 脚本绑定：嵌入 Lua 或自研极简 bytecode VM | M2 | ★★★★★ | 反射系统的终极检验；`03-learning-map.md` 有阅读材料 |
| S2 | `YR_BIND_METHOD` 方法绑定 + 从脚本调用 | M2 | ★★★★ | 对照 Godot `method_bind.h` |
| S3 | ECS 数据层：把 Component 数据搬进 SparseSet/Archetype，与 Object 树并存 | M4 | ★★★★★ | 对照 EnTT/flecs；写 benchmark 说明两种范式适用场景 |
| S4 | `IPhysicsBackend` + 玩具 AABB/扫掠实现 + 固定步长插值 | M4 | ★★★ | 理解"物理作为 Server" |
| S5 | 独立渲染线程 + command queue | M10 | ★★★★ | 对照 `server_wrap_mt_common.h` |
| S6 | 迷你 Frame Graph（pass 声明读写 → 自动 barrier） | M10 | ★★★★★ | Yo_Renderer roadmap §1.3 的延续 |
| S7 | 热重载（场景/资源/代码） | M11 | ★★★ | 演示效果极佳 |
| S8 | ImGui 编辑器（Hierarchy/Inspector 可编辑 + undo/redo） | M11 | ★★★ | 对照 `core/object/undo_redo.h` |
| S9 | 内存：自定义 allocator / pool / 分配统计 | M1 | ★★★ | 对照 `core/os/memory.h` |
| S10 | Profiler：内建采样 profiler + 火焰图导出 | M4 | ★★★★ | 对照 `core/debugger/engine_profiler.h` |
| S11 | 网络：确定性锁步或状态同步（用 M8 的游戏做 demo） | M8 | ★★★ | 会倒逼"确定性"设计 |
| S12 | Windows 移植 | M10 | ★★ | 工程体检 |

---

## 节奏建议

**每周（5~8h）**
- 10min：挑 ≤3 个 checkbox（**先规划再动手，这一步不能省**）
- 3~5h：编码（先头文件 + 测试，再实现）
- 1h：Godot 对照阅读（`04-godot-study.md` 对应主题）——结论在对话里过，有架构影响才写 ADR
- 5min：更新 checkbox 与根 README 里程碑表

**维持周**（课业忙时）：只读 Godot 源码 + 在对话里讨论设计，不写代码。允许，但**不允许连续两周**。

**里程碑收口 checklist**（每个 M 结束时做）
- [ ] 全部 checkbox 打勾或移入 Stretch，并写日期；**与实际做法不一致的条目要改写而不是硬勾**（M0 就出现过这种情况）
- [ ] 验收标准逐条自测通过（测试输出就在 CI 记录里，不另存证据文件）
- [ ] "可运行物"录屏或截图存进 `doc/media/`
- [ ] 对着架构图能脱稿讲清这个里程碑新增的模块：为什么存在、和谁协作、失败模式是什么
- [ ] `00-vision.md` §4 成功判据里相关条目打勾
- [ ] 新增/推翻的决策写进 `06-decisions.md`
- [ ] `07-portfolio.md` 更新（这个里程碑能往简历/博客里写什么）—— M8 之前可以跳过
- [ ] git tag：`v0.M?`
