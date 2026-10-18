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
| M3 | 事件系统（单线程 EventBus） | 1~2 | `yr_event` | 确定性重放 + 1000 事件正确性测试全绿（MessageQueue 与跨线程 post 推迟） | ★★ |
| M4 | 场景树与主循环 | 3~4 | `yr_scene` `yr_engine` | **headless 世界 tick 10000 帧**（可录屏终端输出） | ★★★★★ |
| M5 | 渲染契约与 NullBackend | 2 | `yr_render_iface` `yr_render_null` | renderer-neutral 快照 dump/replay + 确定性统计 | ★★★ |
| M6 | 最小 Vulkan Bootstrap | 2~3 | `yr_render_vulkan` | 窗口 + 清屏 + 硬编码三角形（不接场景） | ★★★★ |
| M7 | Snapshot 驱动画面 | 3~4 | `apps/render_demo` | **Runtime 移动 Node → 画面变化**（首个主要作品节点） | ★★★★★ |
| M8 | 序列化与场景资源 | 3~4 | `yr_serialize` | 手写 `.yrscn` 加载 + save→load→save 逐字节 round-trip | ★★★★★ |
| M9 | 资源系统 | 2~3 | `yr_asset` | 异步加载不卡帧 + AssetHandle→GPU 资源桥接 | ★★★★ |
| M10 | 任务系统 | 3~4 | `yr_job` | 并行 vs 串行 benchmark + TSan 干净报告 | ★★★★ |
| M11 | 工具链与打磨 | 2~4 | `tools/yr_pack` `yr_scene_conv` | `.yrpak` 打包 + 覆盖率 | ★★★ |

**总计 ≈ 26~38 周**（约 6~9 个月）。假期按 3 倍速折算。

### 三个铁规则
1. **不跳里程碑**。M2（反射）没做完就开始 M8（序列化）= 返工；M4 的最小闭环没走完就冲 M6/M7（Vulkan）= 把渲染写成绕过 Node 生命周期、延迟删除与帧阶段的胶水。唯一例外：M9 可以先做同步加载版本。
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
| `tsan` / `ci` preset | **推迟**（tsan→M10，ci→Day 5） | 决策协议 R3：不痛就不做 |
| ccache | **推迟到 Day 5** | 它是"提速"不是"能力"；做 CI 缓存时才知道省了多少 |
| `.editorconfig` | 未建 | clang-format 已覆盖格式；需要时再加 |
| `tests/core/test_main.cpp` | 实际叫 `test_smoke.cpp` | 用 `Catch2::Catch2WithMain`，不需要自定义 main（M2 注册反射类型时才需要换） |
| sanitizer 做成 `cmake/YrSanitizers.cmake`（列表解析 + 互斥检查） | **5 行 `option()` 写在顶层** | TSan 到 M10 才用；避免引入 `foreach`/`list(FIND)`/`FATAL_ERROR` 三组新语法 |
| log 的 fmt 风格格式化 | **推迟**（架构已就位） | 格式化是叶子功能，不改接口形状；先确认 `std::format` 可用性再定方案，别手写解析器 |

### Day 7 收口（已完成，2026-09-26）

- [x] `LICENSE`（MIT）✅ 2026-09-26
- [x] 根 `README.md` 加 CI 徽章，里程碑表 M0 → ✅ ✅ 2026-09-26
- [x] `git tag v0.M0` ✅ 2026-09-26
- [x] log 线程安全（单 mutex 保护 handler / level / reportLog）✅ 2026-09-26

**继续推迟，非阻塞**：
- [ ] log 的 fmt 风格格式化（`{}` 占位符）。当前宏只接收 tag + fmt；`test_log.cpp` 的 `[wip]` 用例守着这条
- [ ] 输出补时间戳 / 帧号 / 线程 ID（`05-engineering.md` §5）
- [ ] `InlineBuffer` SBO 优化（有稳定消费者后再做）
- [ ] 装 ccache 并接进 CMake（等 CI 时间成为痛点再做）

### 验收标准

- `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` 三条命令全绿；冷启动 < 60s、热构建 < 5s（**实测：configure 0.15s / 全量 1.2s / 增量 0.18s** ✅）
- 故意在 `yr_core` 里 include 一个上层头文件 → `check_deps.py` 报错并让 CI 失败 ✅ 2026-09-24（已实测）
- 故意写一个 `new` → CI 失败 ✅ 2026-09-24（已实测）
- GitHub Actions 徽章出现在 README，且 matrix 全绿

**学习点**：CMake target vs 变量、generator expression、`target_*` 的 PUBLIC/PRIVATE/INTERFACE 传播、preset 的三种类型、ODR、`-Wshadow` 抓到的第一类真 bug、sanitizer 的插桩原理与代价。
**常见坑**：① `file(GLOB_RECURSE)` 新增文件不触发重新配置 → **本项目禁用 GLOB，显式列源文件**；② `include(Catch)` 漏写 → `catch_discover_tests` 未定义；③ `enable_testing()` 必须在 `add_subdirectory(tests)` 之前；④ sanitizer flag 编译与链接都要加，否则 `undefined reference to __asan_init`；⑤ `cmake --build --preset X` 不会自动 configure；⑥ GCC 的 `-Wshadow` 不抓"局部变量遮蔽函数名"，只抓变量遮蔽变量。
**Godot 对照**：`SConstruct` + `methods.py` + `core/SCsub`（看它如何组织"每目录一个构建脚本"），对照你的 CMake 分层。
**降级方案**：CI 只跑 gcc + debug；clang-tidy 可选，不阻塞 M0 完成。

---

## M1 · 句柄、容器、时间（1~2 周）

**目标**：把"引用一个可能已死的对象"这件事一次性解决，并建立第一个性能测量习惯。

- [x] `yr/core/handle.h`：`Handle<T>`（index+generation 打包、值语义、可哈希、`[[nodiscard]]`）✅ 2026-09-26
- [x] `yr/core/slot_map.h`：`SlotMap<T>`（稠密数组 + 空闲链 + generation）✅ 2026-09-26
- [ ] `yr/core/sparse_set.h`：吸收 yo_lib `yo_sparse_set.h`，**重构**：`int` -1 哨兵 → `uint32_t npos`、`remove_unstable`/`remove_stable` 两种语义、加迭代器
      ⏸ **已暂缓到 M4**：等 Transform SoA 有真实消费者再做。现在做就是造一个没人用的容器（R3）
- [x] `yr/core/string_id.h` / `string_interner.h`：`StringId` 字符串驻留（hash → 稳定节点指针，`intern()` / `view()`，进程级单例，只追加）✅ 2026-09-30
- [x] `yr/core/timer.h`：`Duration`/`TimePoint`（`std::chrono` 别名）+ `ScopeTimer`；完整 `Clock` 推迟到 M4 ✅ 2026-09-27
- [x] `yr/core/object_id.h`：`ObjectID`（全局唯一 64-bit 强类型）；有效 ID 分配与 `ObjectDB` 实现留到 M2 ✅ 2026-10-01
- [ ] `yr/core/math.h`：最小 `Vec2/Vec3/Vec4/Mat4/Transform3D/Quaternion`（决策 Q1；先做 Vec3 + Mat4 + Transform3D，其余按需）
      ⏸ **已暂缓到 M4**：等 Transform 层级真正需要时再做（R3）；实际落地为**独立层** `yr/math`（`yr::math`，见 M4 清单），不在 `yr/core`。
- [x] 测试：Handle 失效检测、SlotMap erase 后旧 handle 失效、slot 复用 ABA、`clear()` 后旧 handle 不复活 ✅ 2026-09-26
- [x] 测试：100,000 次 SlotMap 混合操作、StringId 驻留一致性与存储增长稳定性 ✅ 2026-09-30
- [ ] **benchmark**：`benchmarks/bench_slot_map.cpp`（SlotMap vs `unordered_map` vs `vector+标记位`，测插入/查找/遍历三项）
- [ ] 用 `perf stat` 记录 cache-miss 差异，写进 `benchmarks/README.md`

**验收标准**
- `Handle<T>` 与 `Handle<U>` 不能隐式互转（编译期测试 `static_assert(!std::is_convertible_v<...>)`）。✅
- SlotMap：erase 后旧 handle 的 `get()` 返回 `nullptr`；index 被复用后旧 handle 依然失效（**这是 ABA 测试，必须有**）。✅
- benchmark 报告：遍历 SlotMap 比 `unordered_map<uint32_t, T>` 快 ≥3x（若没有，分析原因并写下来——分析比数字重要）。
- ASan preset 下全部测试干净（2026-09-26 实测 52/52 通过）。

**产出物**：`benchmarks/` 下的 benchmark 代码 + `benchmarks/README.md` 里的数据表与结论（方法必须可复现：硬件、编译选项、迭代次数、如何防止被优化掉）。
**学习点**：位打包、稠密/稀疏数组、cache line、false sharing 初体验、`std::chrono`、benchmark 方法论（预热、多次取中位数、防编译器优化掉）。
**常见坑**：① generation 溢出回绕导致 ABA（40 bit 下需 2^40 次同 slot 复用，实际不可达；回绕后的 ABA 已知并接受，不做注入机制——见 START-HERE R5；但 generation 自增已统一掩码到 40 位并在回绕时跳过 0，边界算术用 helper 静态测试覆盖）；② `SlotMap::get` 返回 `T*` 后容器扩容导致指针失效（文档写明"指针只在本帧有效"）；③ StringId 的哈希冲突（用"hash 定位 + 字符串比较确认"，不要只信 hash）。
**Godot 对照**：`core/templates/rid.h` + `rid_owner.h`（generation + slot）、`core/object/object_id.h`、`core/string/string_name.h`（StringName 的实现，含 512 个 slot 的 hash 表）。
**降级方案**：`SparseSet` 与数学库已直接推到 M4（见上方 ⏸ 标记），本里程碑不再包含它们。

### M1 当前执行拆分（2026-09-26 更新）

为避免一次引入过多概念，实际按下面顺序推进：

| 子阶段 | 内容 | 状态 |
|---|---|---|
| M1a | `Handle<T>` + `SlotMap<T>` + ABA / clear / reserve 测试 | ✅ |
| M1b | 100,000 次固定种子混合操作测试 | ✅ 2026-09-27 |
| M1c | 最小计时基础（Duration / TimePoint / ScopeTimer） | ✅ 2026-09-27 |
| M1d | E1 benchmark（SlotMap / unordered_map / vector+freelist） | 🔨 框架、插入/查找/删除/遍历完成，待 perf 与结论 |
| M1e | `StringId` / `StringInterner` | ✅ 2026-09-30 |
| M1f | `ObjectID` 基础值类型 | ✅ 2026-10-01 |
| 暂缓 | `SparseSet`、数学库 | 等真实消费者 → 已在 M4 清单里登记（见 M4 顶部） |

### M1 收口规则（2026-09-30）

M1 的核心学习目标已经完成大半。剩余 `ObjectID` 只做值类型、比较、哈希和 invalid 语义；`ObjectDB` 明确留到 M2。E1 只补一次 `perf stat` 与结论，不再为 M1 增加新的容器或优化。完成这两项后立即进入 M2，避免基础设施阶段无限延长。

### M0~M4 时间线校准（2026-09-30）

| 阶段 | 推荐投入 | 目标 | 必须出现的可运行物 |
|---|---:|---|---|
| M0~M1 | 3~4 周 | 工程地基、句柄、字符串、时间、性能测量 | `ctest` 全绿 + E1 数据 |
| M2 | 2~3 周 | ObjectID/ObjectDB、Variant、最小 ClassDB、反射查看 | `yr_inspect --all` |
| M3 | 1~2 周 | 单线程 EventBus（Subscription/post/flush/重入）；MessageQueue 推迟 | 确定性重放 + 1000 事件测试 |
| M4 | 3~4 周 | Node、SceneTree、MainLoop、延迟删除、headless tick | `yr_runtime_headless --frames N` |

按 5~8 小时/周计算，M4 应在项目开始后约第 7~10 周出现，这个位置合理。M2 超过 3 周时，应砍掉方法绑定、复杂 Variant 类型和工具输出细节，而不是继续推迟 M4。

---

## M2 · 反射与对象模型（2~3 周）★ 项目分水岭

**目标**：让 C++ 类型在运行时可被"看见"。这是序列化、检视器、事件、脚本的共同地基，也是本项目最有含金量的模块。

- [x] `yr/object/variant.h`：Variant（10 个 tag：null/bool/int/float/StringId/ObjectID/string/array/dict/vec3）+ 完整测试（类型混淆、拷贝、比较、Dict 保序、深拷贝/移动 Rule of Five）✅ 2026-10-05
- [ ] `yr/object/object.h`：`Object` 基类 + `notification(uint32_t)` + `ObjectID`（其中 ObjectID RAII 自动注册/注销、禁止复制移动已完成 ✅ 2026-10-01；notification 待后续）
- [x] `yr/object/object_db.h`：`vector<ObjectSlot> + freelist` 的非拥有 `ObjectID → Object*` 索引；注册/注销/查询、generation 复用、存活计数已完成。类型分布统计等反射就位后再补 ✅ 2026-10-01
- [ ] `yr/object/ref_counted.h`：`RefCounted` + `Ref<T>` + `WeakRef<T>`（走 ObjectDB）
- [x] `yr/object/property_info.h` + `class_info.h`：`PropertyInfo`（name/type/flags/class_hint + getter/setter 适配器）+ `ClassInfo`（属性表 + get/set + 继承链 + 遮蔽）。手工注册，无宏；factory/default 值留到 M2e/M5 ✅ 2026-10-05
- [x] `yr/object/class_db.h` + `register_core_classes()`：`unordered_map<StringId, const ClassInfo*>` 注册表 + `registerClass`/`getClass`/`allClasses`/`inheritorsOf`/`freeze`；显式注册入口 + SIOF 测试。`instantiate` 暂返回裸指针（所有权待 `Ref`，M8 再定）✅ 2026-10-06
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
**常见坑**（这个模块的坑最多。踩到了就在 commit message 正文里写一句，或讲给 AI 听）：
① 宏里用 `decltype(member)` 推导失败 → 需要 `Type::*` 成员指针而非直接取地址；
② 静态注册器在动态库里被链接器丢弃（`--whole-archive` 问题）→ 本项目全静态库，但要知道这个坑；
③ `ClassInfo` 里的 lambda 捕获导致不能转函数指针 → 用无捕获 lambda 或 `void*` 成员偏移；
④ Variant 的 `Dict`/`Array` 深拷贝性能 → 先不优化，测出来再优化；
⑤ 属性默认值存储：放 `ClassInfo` 里（每类一份）而不是每对象一份。
**Godot 对照**：`core/object/object.h`（`GDCLASS` 宏、`_bind_methods`、`notification`）、`core/object/class_db.h/.cpp`（`bind_property` / `ClassInfo` 结构 / `instantiate`）、`core/object/property_info.h`、`core/object/method_bind.h`（方法绑定的类型擦除，比属性绑定更难，选读）、`core/register_core_types.cpp`（显式注册顺序）、`tests/core/object/test_class_db.cpp` + `test_object.cpp`（**看成熟项目怎么测反射**）。
**降级方案**：只做 `YR_CLASS` + `YR_PROPERTY`，不做方法绑定（`YR_BIND_METHOD` 推到 stretch）；Variant 只做 6 种类型（null/int/float/string/vec3/objectid）。

### M2 当前执行拆分（2026-10-01）

| 子阶段 | 内容 | 状态 |
|---|---|---|
| M2a | `yr_object` target + ObjectDB 非拥有索引 + ObjectID ABA 测试 | ✅ |
| M2b | `Object` 自动注册/注销、`id()`、禁止复制移动与生命周期测试 | ✅ 2026-10-01 |
| M2c | 最小 `Variant`（10 个 tag，含 string/array/dict/vec3） | ✅ 2026-10-05 |
| M2d | `PropertyInfo` / `ClassInfo`，先手工注册 | ✅ 2026-10-05 |
| M2e | `ClassDB` + 显式注册顺序 | ✅ 2026-10-06 |
| M2f | `YR_CLASS` / `YR_PROPERTY` 宏化 | ⬜ |
| M2g | `yr_inspect` | ⬜ 暂缓（等 M4（Node）有真实类再做） |

---

## M3 · 事件系统与延迟调用（1~2 周）

**目标**：让模块之间不需要互相 include 就能通信，并解决"回调里改容器"的经典难题。

**收口范围（2026-10-08）**：本轮完成的是**单线程 EventBus**（核心 / 单线程范围完成）。
`MessageQueue` 与跨线程 `post` 未实现，推迟理由见下。

- [x] `yr/event/subscription.h`：强类型 `Subscription`（RAII，析构自动退订）✅ 2026-10-08
- [x] `yr/event/event_bus.h`：按 §4.5 重构（删 `lastMsg_`、强类型 token、`publish`/`post` 分离、双缓冲队列、`globalEventBus`）✅ 2026-10-08
- [ ] `yr/event/message_queue.h`：`MessageQueue`（deferred call：`ObjectID` + `StringId` + `vector<Variant>`；flush 期间新增进下一帧）
      ⏸ **推迟**：依赖尚未实现的**方法绑定**与未来 `MainLoop` 的**安全 flush 点**（§4.5 / 帧阶段 2）。它不是纯多线程功能，和方法绑定/M4 一起具备后再做
- [x] 重入策略实现 + assert：发布深度上限 8（第 9 层为编程错误，Debug assert 且拒绝，不转 post）、退订自己安全、flush 双缓冲 ✅ 2026-10-08
- [ ] 线程约束：`EventBus::publish` 加主线程 assert；提供 `post_from_any_thread`（MPSC，帧首合并）
      ⏸ **推迟到 M10**：与线程模型、TSan 一起做；单线程契约目前只写在头文件/架构注释里，不加 assert
- [ ] 把 `Object::notification()` 与 EventBus 的分工想清楚（notification = 定向、沿继承链传播；event = 广播、跨模块），结论写进两者的头文件注释
      ⏸ 随 `Object::notification` 本身（M4 的 Node 生命周期通知）一起做
- [x] 测试：§4.5 表格里那三条语义各一个用例；1000 事件/帧的压力测试；订阅者抛异常/退订其他订阅者的边界测试；确定性重放 ✅ 2026-10-08
- [ ] 事件追踪：`YR_LOG_DEBUG` 打印每次 publish 的类型与订阅者数量（调试期极其有用）
      ⏸ **非阻塞优化**：不阻塞 M3 收口，需要调试时再加

**验收标准**
- [x] §4.5 三条语义测试全绿（**这三条是模块的真正交付物**）✅ 2026-10-08
- [x] `Subscription` 析构后不再收到事件（RAII 测试）✅
- [x] 同一帧内事件派发顺序确定（同样输入两次运行完整回调日志逐项相同，使用局部 EventBus，不依赖全局残留）✅ 2026-10-08
- [ ] TSan preset 下 `post_from_any_thread` + 主线程 flush 无竞争报告 ⏸ 推迟到 M10

**产出物**：确定性重放测试（同输入两次运行完整回调日志逐项相同）✅ 2026-10-08。
**学习点**：`std::function` 的堆分配与小对象优化、重入与迭代器失效、RAII 句柄（已覆盖）；变参模板、MPSC 队列随 M10 一起补。
**常见坑**：① `std::function` 捕获大对象导致每次订阅都堆分配（性能实验推迟，非阻塞）；② 事件类型用 `typeid` 做 key 的 RTTI 开销与跨 TU 一致性（当前保留 `type_index`，理解代价即可）；③ flush 中 post 导致死循环（双缓冲已解决，递归 flush 有 assert）。
**Godot 对照**：`core/object/object.h` 的 signal（`connect`/`emit_signal`/`Callable`）；`core/object/message_queue.h/.cpp` 与 `core/object/callable_mp.h` 推迟到 MessageQueue 解禁后再精读。
**降级方案**：`post_from_any_thread` 推到 M10（已执行）；`MessageQueue` 整体推迟到方法绑定 + `MainLoop` 安全点具备后（已执行）。

---

## M4 · 场景树与主循环（3~4 周）★ 运行时的灵魂

**目标**：**第一次有一个"活着的世界"**。这个里程碑结束时，你应该能在终端看到一个世界 tick 一万帧，节点在创建、更新、销毁。

### M4 当前执行拆分（2026-10-12）

一次只引入一个概念，每步独立可验收。**不要一次实现整章。**

| 子阶段 | 唯一新概念 | 交付物 | 独立验收点 | 状态 |
|---|---|---|---|---|
| **Node-1** | 父子关系与所有权 | `yr/scene/node.h` + `src/node.cpp` + `tests/scene/test_node.cpp` | 单父不变量：不能加自己 / 多父 / 成环；`addChild` 转移所有权、`removeChild` 交出所有权 | ✅ 已完成（2026-10-12） |
| **Node-2** | SceneTree 内状态与生命周期传播 | `enter` / `ready` / `exit` 派发 + selective propagation guard | 三层树断言：enter 前序（父先子）、ready 后序（子先父）且只一次、exit 后序；回调中越界修改被拒 | ✅ 已完成（2026-10-13） |
| **Node-3** | 延迟删除 | `queueFree` + `ObjectID` 删除队列 + 显式 `flushDeletions` 安全点 | 自己 / 父 / 子 / 兄弟四种删除场景不 double free；父先删则子 ID 解析失效 | ✅ 已完成（2026-10-14） |
| **Node-4** | process 前序遍历 | `SceneTree::process(dt)`：**全树前序 `ObjectID` 快照** + 逐个经 `ObjectDB` 重解析派发 `onUpdate` | 前序顺序（父先子、兄弟按 children 顺序）；回调中新增本帧不 process；回调中删除靠 ID 失效容忍；重入拒绝 | ✅ 已完成（2026-10-15） |

**Node-1 已决策（2026-10-12，详见 `START-HERE.md` §2）**：
① 父拥有子用 `std::unique_ptr<Node>`（旧草图的 `Ref<Node>` 未实现，已弃用）；
② `addChild` 失败时**不消费**参数，所有权仍归调用者（`std::unique_ptr<Node>&&` 强失败保证）；
③ `queueFree` 标志放 `Object` 还是 `Node` 留到 Node-3 决定。

> Node-1 已落地 `yr/scene/node.h`（`yr_scene` target）+ `YrTests_scene`；生命周期/更新回调仅占位 no-op。
> Node-2 动笔前读 `01-architecture.md` §4.6 与 `04-godot-study.md` 的 "Node 生命周期" 行。
>
> **Node-2 完成（2026-10-13）**：enter/exit、ready 与 selective propagation guard 均已完成，落地
> `scene_tree.h/.cpp` + `node.h/.cpp` + `test_scene_tree.cpp` + `test_lifecycle.cpp` + `test_ready.cpp`
> + `test_propagation_guard.cpp`。`Node::setTree/propagateEnterTree/propagateExitTree/propagateReady`
> 为私有传播函数（`friend SceneTree`）；`SceneTree::setRoot/clearRoot/析构`、`Node::addChild/removeChild/detach`
> 已接线。ready 采用**单标记 `everReady_`**（不是 `entered` + `ready_requested` 双状态）：后序、子先父、
> 一生只一次；`everReady_` 在 detach/exit/clearRoot 不重置，重新入树只重新 enter；`setRoot` 在整树
> enter 前序完成后统一 ready，`addChild` 对已 ready 的树内父节点让新子树 enter 后立即 ready。
> propagation guard 采用**私有传播深度计数 `propagationDepth_` + 最小 RAII guard**（不是 bool）：
> 只冻结“正在被遍历的节点自身 children_”——enter/ready 中允许修改自己的 children，禁止 child 回调修改
> 正在被遍历的 parent，exit 期间冻结当前节点结构；`requestReady()` 不实现。
> **Node-3 延迟删除已完成（2026-10-14）**：`Node::queueFree`（重复调用幂等；非树内 Debug 断言 +
> 所有构建安全返回、不置标记）+ `SceneTree` 的 `ObjectID` 删除队列 + `flushDeletions`
> （开始时 swap 快照、重入防护，子 / root / 孤儿三分支，孤儿断言跳过不删除）。测试
> `test_queue_free.cpp`：自己 / 父 / 子 / 兄弟四种场景不 double free（ASan 验证）、父子同帧两种
> 入队顺序、flush 中 exit 回调再 queueFree 延后一批、非树内 queueFree 与孤儿跳过。
> **Node-3.5 收尾已完成（2026-10-14）**：`addChild`/`removeChild` 在入口 `propagationDepth_`
> 检查后立即对接收者 this 加 RAII 结构锁，冻结其 `children_` 覆盖整个结构操作——removeChild
> 覆盖 find_if → `setTree(nullptr)`（整棵被摘子树 exit 回调）→ move/erase → 清 parent_；
> addChild 覆盖树内分支的 `setTree` 与 `propagateReady`。回调中对同一 parent 的
> addChild/removeChild/detach 一律被拒（断言 + 返回失败，不消费）。手动摘除复位
> `queuedForDeletion_`（取消删除意图，可重新 queueFree）。测试补结构锁 / 恢复性 / 重排队用例，
> Debug/Release/ASan 全绿。
>
> **Node-4 process 遍历已完成（2026-10-15）**：`SceneTree::process(float)` 开始时一次性
> **前序收集整棵子树的 `ObjectID`**（非 `Node*`）到局部快照，派发阶段逐个经
> `obj::globalObjectDB().get(id)` 重解析：解析失败（节点已级联销毁 / 注销）或解析成功但
> `!isInsideTree()`（回调中被 detach）一律跳过，其余调用 `onUpdate(deltaTime)`。**采用全树快照
> 方案 A**（而非 Godot 的注册列表）：第一版所有树内节点都 process，不实现 `setProcess` 开关、
> priority 排序、暂停、physics process。快照语义使回调中 `addChild` 的新节点本帧不入快照、
> 下一帧才收集到；回调中 `removeChild`/`detach`（含丢弃所有权立即析构）与 `queueFree` 均安全，
> 无需冻结结构——这正是选 `ObjectID` 而非 `Node*` 的核心原因（`Node*` 会悬垂）。
> 重入用 `processing_` + RAII guard 拒绝（`assert` + 早退）；对称地 `flushDeletions` 在
> `processing_` 期间也被拒，新删除留到下一次 flush；两者分离，由调用方按帧阶段先后调用。
> 测试 `test_process.cpp` 10 例：前序顺序、快照新增、自己/父 `queueFree`、丢弃所有权立即析构的
> ASan 用例、detach 保留所有权跳过、重入、process 期间 flush 拒绝、空树、多帧确定性；
> Debug/Release/ASan 全绿。
> **Node-4 范围外（= M4 剩余项）**：Transform 层级、`NodePath`、MainLoop 14 阶段、固定步长、
> headless 平台、`FrameStats`、Engine/runtime_headless、10000 帧集成测试，以及 process 分组/`setProcess`/暂停/physics。

- [x] **（从 M1 推来）** 最小数学库：只做 Transform 需要的部分（`Vec3` + `Mat4` + 平移合成）；实际落在 `yr/math`（`yr::math`，而非 roadmap 早先写的 `yr/core`）✅ 2026-10-16
- [ ] **（从 M1 推来，可选）** `yr/core/sparse_set.h`：**只在 S3（ECS 数据层）真要做时才写**。Transform 用普通 SoA 数组就够，不必先造 SparseSet
- [ ] `yr/scene/node_path.h`：`NodePath`（`"Root/Level/Player"` 解析、相对路径 `../`）——**未做**，非 M4 可运行物阻塞项
- [x] `yr/scene/node.h/.cpp`：`Node`（父子关系、`addChild`/`removeChild`/`detach`、`queueFree`、`isInsideTree`、`setPosition`）；`get_node(path)` 与名字唯一性未做 ✅ Node-1~4（2026-10-12 ~ 10-15）
- [x] 生命周期：`onEnterTree`（前序）/ `onReady`（后序、一生一次）/ `onUpdate`（前序）/ `onExitTree`（后序倒序），顺序由测试锁定；用 protected virtual 回调而非 `notification(uint32_t)` 编号 ✅ 2026-10-13 ~ 10-15
- [x] `yr/scene/scene_tree.h/.cpp`：`SceneTree`（拥有 root、`process` 前序快照、`refreshTransforms`、`flushDeletions`）；`SceneTreeTimer` 推到 M5 ✅
- [x] Transform 层级：`position` 单一数据源 + `Handle<TransformData>` 槽 + 脏标记向下传播，**降级为仅平移**（roadmap 降级方案）✅ 2026-10-16
- [x] `yr/engine/main_loop.h/.cpp` + `clock.h`：§5 的 14 个阶段（空钩子保留），tick 编排 + 四安全点接线；`Clock` clamp 在采样侧 ✅ 2026-10-16 ~ 10-17
- [x] 固定步长累加器 + spiral-of-death 保护（单帧最多 8 步 + fmod 截断 + warn-once）+ `timeScale` + `paused` ✅ 2026-10-17
- [x] `yr/engine/engine.h/.cpp`：最小 `Engine`——组合拥有 `SceneTree`/`EventBus`/`Clock`/`MainLoop`，`run(maxFrames)` 用真实 `Clock` 驱动 `MainLoop::tick`；帧阶段顺序仍唯一定义在 `MainLoop` ✅ 2026-10-17
- [x] `yr/engine/frame_stats.h/.cpp`：每阶段耗时（`ScopeTimer`）+ 环形窗口 + 每 N 帧打印 p50/p95/max + 可选极简 CSV ✅ 2026-10-17
- [x] headless 可运行物：不单独建 `yr/platform/headless.h`，改由 `apps/runtime_headless --frames N` 直接以墙钟 `Clock` 驱动无窗口 tick；输入注入 / 脚本平台层推迟 ✅ 2026-10-17
- [x] `apps/runtime_headless`：极薄正式 host，解析 `--frames N`，创建 Engine 跑 N 帧并打印 FrameStats 汇总 + ObjectDB 存活数（**M4 的"可运行物"**）✅ 2026-10-17
- [x] 集成测试：跑 10000 帧（ASan 下 1000）→ 断言帧数、物理步数、事件数、中途删除、Transform 刷新、百分位、无泄漏 ✅ 2026-10-17

### M4 收口总结（2026-10-17）

**已完成（含一句关键决策）**

- **Node-1~4**（`engine/scene/include/yr/scene/node.h`、`scene_tree.h`）：父子所有权用 `std::unique_ptr<Node>`，失败不消费参数；`Handle<TransformData>` 的生命周期 = 首次入树分配 / detach 保留 / Node 析构释放；重入树标脏；`queueFree` 走 `ObjectID` 删除队列，三个安全点（`process` / `refreshTransforms` / `flushDeletions`）互斥且显式由调用方按帧阶段排序。
- **Transform 层级**：`position_` 是唯一可写数据源（`setPosition` 只标脏），`globalTransform()` 返回上次 `refreshTransforms` 的缓存；本阶段降级为仅平移（无旋转/缩放）。
- **MainLoop**（`engine/engine/.../main_loop.h`、`clock.h`）：14 阶段骨架，本阶段接线 0/3/6/7/8/9/13（`beginFrame` 计数与累积、`EventBus::flush`、固定物理循环、`SceneTree::process`、`refreshTransforms`、`flushDeletions`、`FrameStats` 汇总）；阶段方法是 `protected virtual`，测试探针可覆写而不侵入生产代码；`Clock` 的 clamp（0.25s）放在采样侧，`tick(double)` 是最终注入点。
- **时间**：累加器吃**缩放后的 gameDelta**（`timeScale`/`paused` 单规则：`effectiveTimeScale = paused ? 0 : timeScale`）；spiral 保护 = 单帧最多 8 步 + `fmod` 丢弃整步积压 + warn-once。
- **FrameStats**（`engine/engine/.../frame_stats.h/.cpp`）：MainLoop **组合**持有，`stats()` 访问；`ScopeTimer` 对 14 阶段前后采样，4096 环形窗口，每 1000 帧打印 **total 的 p50/p95/max，各阶段只打 p50**；CSV 极简附加（`frame,total_ns,physics_steps`，默认关闭）。
- **Engine / runtime_headless**（`engine/engine/.../engine.h/.cpp`、`apps/runtime_headless`）：`Engine` 组合拥有 `SceneTree` + 局部 `EventBus` + `Clock` + `MainLoop`（声明顺序保证 `MainLoop` 最先析构、`SceneTree` 最后析构），`run(maxFrames)` 用真实 `Clock` 驱动 `MainLoop::tick`，到帧数上限或 `shouldQuit()` 退出；host 只解析 `--frames N`、创建 Engine 并报告 FrameStats 汇总，不生成演示场景。**阶段顺序仍唯一定义在 `MainLoop`，Engine 不复制。**
- **10000 帧集成测试**（`tests/integration`）：固定 `1/60` 确定性推进，一次覆盖帧数 / 物理步数 / 事件顺序 / 中途删除 / Transform 刷新 / FrameStats 百分位 / ObjectDB 归零。

**与 Godot 的对照要点**

- Godot 的 `MainLoop`（`main/main.cpp` 的 `Main::iteration()`）是**帧骨架**，但 YRuntime 不把它当"世界接口"——SceneTree 只经 `process/refreshTransforms/flushDeletions` 三个显式安全点被驱动，没有隐藏的遍历。
- Godot 把安全点分散在 `SceneTree` 与 `MessageQueue` 内部；YRuntime 把它们**显式写在骨架层**（阶段 3/7/8/9），顺序即正确性。
- Godot 的 `MainTimerSync`（固定步长 + 追帧上限）对应 YRuntime 的累加器 + spiral 截断；差异是 YRuntime 的累加器消费缩放后 delta，暂停即 timeScale 0。

**测试规模与三配置**

| 配置 | 结果 | 备注 |
|---|---|---|
| Debug | 296/296 | 集成测试 10000 帧 |
| Release | 287/287 | 集成测试 10000 帧；DEBUG 断言的用例在 Release 下不计入 |
| ASan+UBSan | 296/296 | 集成测试经 `YR_INTEGRATION_FRAMES` 编译宏降到 1000 帧 |

**M4 剩余 / 遗留（不阻塞收口）**

- 未接线阶段 1/2/4/5/10/11/12 仍是空钩子（`poll_input` / `flush_messages` / `pump_assets` / `pre_physics` / `extract_render` / `present` / `collect_garbage`）。
- `MessageQueue` 推迟（依赖方法绑定 + 阶段 2 安全点）；`onPhysicsUpdate` 未加（无消费者）；`Object::notification` 编号体系未引入。
- `NodePath`、名字唯一性、`get_node(path)`、独立 headless 平台层未做。
- Transform 仅平移；CSV 仅 total + physics 步数（完整每阶段列与绘图归 M11）。

**下一步（按路线图）**：进入 **M5 · 渲染契约与 NullBackend**——`yr_render_iface`（`RenderSnapshot`/`RendererBackend` 纯 POD 契约）、`yr_render_null`，以及 `yr/engine/render_extract.h/.cpp` 把阶段 10 接上 `SceneTree → RenderSnapshot` 的单向抽取；快照 dump/replay 作为契约完备性验收。

**验收标准**
- **生命周期顺序测试**：嵌套 3 层树，`_ready` 后序 / `_process` 前序 / `_exit_tree` 顺序，逐条断言。
      ✅ `_ready`/`_exit_tree` 由 Node-2（`test_lifecycle.cpp`）锁定，`_process` 前序由 Node-4（`test_process.cpp`）锁定。
- **删除安全测试**：节点在自己的 `kProcess` 里 `queue_free()` 自己 / 父节点 / 子节点 / 兄弟节点，四种情况都不崩溃（ASan 干净）。这是本里程碑的核心价值。
      ⚠ 部分：`onUpdate` 中 self / parent 已由 `test_process.cpp` 覆盖（ASan）；child / sibling 的延迟删除语义由 Node-3 `test_queue_free.cpp` 覆盖，尚未在同一条 `kProcess` 用例中同时覆盖四种。
- **遍历中新增**：`kProcess` 里 `add_child` 的新节点本帧不 process（与 Godot 一致），测试锁定。
      ✅ Node-4：`test_process.cpp` 的 "a node added during a callback..." 用例。
- 10000 帧 headless 运行：ASan+UBSan 干净、ObjectDB 归零、帧耗时标准差合理（打印 p50/p95/max）。
- 固定步长：把 `real_delta` 人为抖动（模拟卡顿），physics 步数依然正确（写测试）。

**产出物**：`yr_runtime_headless --frames N` 终端输出 + `FrameStats` CSV。帧阶段顺序的定义写在 `01-architecture.md` §5，代码里的注释指向它。
**学习点**：组合模式、树遍历与迭代器失效、脏标记、通知模式、固定步长积分、性能采样、RAII 与延迟销毁。
**常见坑**：① `add_child` 时忘记设置 parent / 忘记派发 `kEnterTree` 到整棵子树；② `queue_free` 的 double free（父节点删了，子节点也在待删列表）；③ `get_node(path)` 每次都做字符串解析（缓存 `NodePath` 解析结果）；④ Transform 脏标记向上还是向下传播搞反；⑤ 暂停时 `SceneTreeTimer` 是否继续走（Godot 有 `process_always`/`process_in_physics` 标志，想清楚再实现）。
**Godot 对照**：`scene/main/node.h` + `node.cpp`（`_propagate_ready`/`_propagate_enter_tree`/`_propagate_exit_tree`/`queue_free`）、`scene/main/scene_tree.h/.cpp`（`process`/`physics_process`/`flush_transform_notifications`）、`scene/main/scene_tree.h:57`（`SceneTreeTimer`）、`main/main.cpp`（`Main::iteration()` —— **一帧的权威顺序，M4 必读**）、`main/main.h`。
**降级方案**：Transform 层级只做 position（不做旋转缩放）；`SceneTreeTimer` 推到 M5；`FrameStats` 只做总耗时。

---

## M5 · 渲染契约与 NullBackend（2 周）★ 解耦的那条线

**目标**：定义 Runtime 与 Renderer 之间那条线，并用一个什么都不画的后端验证它。本阶段只做 **renderer-neutral 数据契约**，不碰任何图形 API；真正的画面留到 M6/M7。

- [ ] `yr/render/snapshot.h`：`RenderSnapshot` 及全部 POD 类型（§4.11），**每个字段的单位/坐标系/矩阵序都写注释**
- [ ] `yr/render/backend.h`：`RendererBackend` 接口 + `BackendConfig` + `BackendStats`
- [ ] `yr/render/asset_handle.h`：**强类型** `AssetHandle`（默认构造 = 无效；后端可有内建 triangle / default material 的临时 ID，正式 `AssetUID` 在 M9 替换）
- [ ] `yr/engine/render_extract.h/.cpp`：`RenderExtractSystem`（遍历 SceneTree → 可见性 → 生成 DrawItem/Camera/Light；本阶段全量抽取，**不做剔除**）
- [ ] 排序键：`sort_key = (pipeline << 48) | (material << 24) | depth`（对照 Yo_Renderer roadmap §2.1）
- [ ] `yr_render_null`：`NullBackend`（统计 draw call / 三角形数 / 快照字节数，校验快照合法性）
- [ ] **快照重放**：`--dump-snapshot f.bin` 把快照写文件，`--replay-snapshot f.bin` 直接喂给 Backend（**验证契约完备性的杀手级测试**）
- [ ] 把 `01-architecture.md` §7 的三个解耦层次逐条对应到代码：哪一行实现了**数据解耦**、哪一行是**生命周期解耦**、哪一行是**时间解耦**（写在 `yr/render/snapshot.h` 的头注释里）
- [ ] CI 检查：`grep` 确保 `engine/`（除 `render/vulkan/`）零 `vulkan`/`GLFW` include
- [ ] 测试：快照 dump→replay 逐字节一致；`NullBackend` 在 10000 帧 headless simulation 里统计数字稳定

**验收标准**
- headless + `NullBackend` 跑 headless scene simulation 1000 帧，快照统计正常，CI 全绿。
- 快照 dump/replay round-trip 一致，**且统计确定**（同输入两次逐字节相同）。
- `yr_render_iface` 的头文件**只 include `yr/core/*` 与 std**（CI 检查 include 列表）。
- `DrawItem` 中**禁止**出现 `Object*` / `Mesh*` / `Ref<Mesh>` / `Vk*`；`AssetHandle` 是强类型，且"未就绪资源本帧跳过"写进契约。
- 能回答："如果换一个 OpenGL 后端，需要改 Runtime 的哪一行代码？"（答案必须是 0 行）

**产出物**：快照重放 demo。**这个主题面试高频，值得写成博客**（见 `07-portfolio.md` §4）。
**学习点**：数据契约设计、POD 与 ABI、Server 模式、可重放性作为设计验证手段。
**常见坑**：① 快照里偷偷放了 `Object*`（"就这一次"）→ 解耦崩塌，CI 必须挡住；② 矩阵行列主序/坐标系（Y-up vs Z-up）没写清 → 后面画面全错且极难查，**现在就把注释写死**；③ 资源上传请求的时序（Backend 还没加载完 mesh，DrawItem 已经引用它）→ 契约里要规定"未就绪的资源本帧跳过"；④ 把临时 triangle / default material ID 当成正式资源系统 → 正式 `AssetUID` / `AssetDatabase` 必须等 M9。
**Godot 对照**：`servers/rendering/rendering_server.h`（Server 命令接口）、`servers/server_wrap_mt_common.h`（选读：Server 如何包装）、`core/templates/command_queue_mt.h`。
**降级方案**：不做快照重放（只做 dump 用于人工检查）。**但"Runtime 侧零 vulkan include"与"AssetHandle 强类型"不能降级。**

---

## M6 · 最小 Vulkan Bootstrap（2~3 周）★ 第一次有画面，但还不是作品

**目标**：把窗口 + 设备 + swapchain + 帧同步这条**纯渲染器工程链路**独立跑通，清屏并画一个硬编码三角形。**不接场景树、不消费快照**（最多只做最小桥接），以免绕过 M4 的 Node 生命周期、延迟删除与帧阶段。

**Yo_Renderer 复用边界（审计结论，必须遵守）**：以**最小 Vulkan backend 重写为主**，选择性借鉴 / 逐文件搬底层机制为辅；**不把整个 Yo_Renderer 作为 subdirectory，也不整体搬迁**。

- [ ] 策略写进 ADR（D19）：哪些**重写**、哪些**逐文件借鉴**，落进 `engine/render/vulkan/` 并适配 YRuntime 命名 / `Handle` / CMake 分层
- [ ] 可借鉴 / 可能搬迁（逐个 review 后适配）：`platform/VulkanContext`、`Swapchain`、`SyncObjects` / `FrameSync`、`rhi/command/CommandRecorder`、`render/VulkanResourceTable`
- [ ] **不搬**：`app/*`、`framework/*`、`system/*`、`ui/*`、`core/ObjectDB/Ref/Handle/TextureCache`、现有 `RenderServer`、`asset/GltfLoader` 全链路（存在反向依赖、`void*`、场景 / GPU 生命周期耦合或与 YRuntime 重复）
- [ ] `yr_render_vulkan` target（CMake option `YR_BUILD_VULKAN_BACKEND`，默认 OFF）：GLFW 窗口 + Vulkan instance/device/queue + swapchain + frame sync
- [ ] 清屏 + 硬编码三角形（顶点 / 着色器写死在 backend 内），validation layer 零 error
- [ ] `VulkanBackend : RendererBackend` 骨架（`initialize`/`shutdown`/`present` 可为空实现或只画上面那一帧）；本阶段**不要求**消费真实 `RenderSnapshot`
- [ ] 明确**不做**：PBR、IBL、shadow、glTF、ImGui、ECS、异步加载、JobSystem、独立渲染线程、视锥剔除
- [ ] CI / 工程：`engine/` 除 `engine/render/vulkan/` 外零 Vulkan/GLFW include；`YR_BUILD_VULKAN_BACKEND=OFF` 时 CI 仍全绿（无 GPU 不构建该 target）
- [ ] 借鉴 / 搬迁时遇到的**耦合点逐条记进 commit message 正文**

**验收标准**
- 能打开窗口、清屏并画出硬编码三角形；validation layer 零 error。
- 反复 resize / 最小化后不崩溃；连续运行 10 分钟无泄漏。
- `engine/`（除 `engine/render/vulkan/`）零 Vulkan/GLFW include；`YR_BUILD_VULKAN_BACKEND=OFF` 构建与 CI 全绿。
- 能回答："这块三角形为什么还没有经过 `RenderSnapshot`？"（答案：为了先把渲染器工程链路与契约接入**分开验证**）

**产出物**：窗口 + 三角形截图 / GIF。**注意：本阶段只是 bootstrap，首个作品节点在 M7。**
**学习点**：Vulkan 初始化与 frame-in-flight 的工程顺序、swapchain 重建、validation layer 使用、最小可重写面 vs 可借鉴底层机制的边界。
**常见坑**：① 想"顺便把快照 / 场景接进来"→ 范围爆炸，本阶段只验证渲染器链路；② 直接把 Yo_Renderer 的 `RenderServer` / ECS 拖进来 → 反向依赖与场景假设回归；③ swapchain 重建时旧资源释放顺序；④ 借来的代码带着 `void*` / 旧 Handle → 适配时统一到 `yr::render::vk` 与 YRuntime 类型的桥接层。
**Godot 对照**：`drivers/vulkan/`（Vulkan 驱动的初始化与 context 组织）、`servers/display/display_server.h`（display/headless 边界）。
**降级方案**：只做清屏不做三角形；窗口事件只处理 close。**"validation layer 零 error 且 Runtime 侧零 include"不能降级。**

---

## M7 · Snapshot 驱动画面（3~4 周）★★ 首个主要作品节点

**目标**：`VulkanBackend` 真正消费 M5 的 `RenderSnapshot`，形成 `游戏状态 → Runtime → RenderSnapshot → RendererBackend → GPU` 的完整链路。**Runtime 里移动一个 Node，画面同步变化**——这是本项目第一个主要作品节点。

- [ ] `VulkanBackend` 完整实现 `RendererBackend`：`present(snapshot)` → DrawItem / Camera / Light 转为 GPU 提交
- [ ] **AssetHandle → GPU 桥接（临时版）**：先用 backend 内建 triangle / default material ID 响应 `AssetHandle`；正式 `AssetUID` / `AssetDatabase` 到 M9 再接
- [ ] 相机 / 光照从快照取，删掉 Backend 内部对"场景"的假设（Backend 不得持有 `SceneTree*`）
- [ ] 快照传递引入双 / 三缓冲（落实 §4.11 的"时间解耦"），frame index 单调递增
- [ ] `apps/render_demo`：Runtime 里搭一个场景（几个带 Transform 的 Node + Camera Node + Light Node），键盘控制 Node → 画面变化
- [ ] validation layer 零 error；`--dump-snapshot` + `--replay-snapshot` 在真后端也能跑，且与实时运行一致
- [ ] `RenderSnapshot` 结构自 M5 起**未因后端需要而修改**（若改了 → 写 ADR 复盘，这本身是有价值的产出）
- [ ] 帧时间统计：CPU 侧 extract / GPU 侧 present 分别可见
- [ ] 录屏 + 截图进 `doc/media/`，README 更新架构图
- [ ] 明确**不做**：视锥剔除、PBR、IBL、shadow、glTF、ImGui、独立渲染线程（都列入 stretch）

**验收标准**
- `apps/render_demo` 运行：键盘移动一个 Node → 画面同步变化；validation layer 零 error；连续跑 10 分钟无崩溃无泄漏。
- **`engine/` 目录（除 `render/vulkan/`）零 Vulkan/GLFW include**（CI 硬门禁）。
- 快照重放：把 M5 dump 的快照喂给 `VulkanBackend`，画面与实时运行一致。
- Runtime 侧（`render_demo` 的游戏逻辑）只通过 Node / SceneTree 的 public API 驱动画面，不直接碰 Vulkan。

**Stretch（做完上面才考虑）**：独立渲染线程（Runtime 主线程写快照，渲染线程消费，对照 `server_wrap_mt_common.h`）；视锥剔除放进 `RenderExtractSystem`；ImGui 只读检视器；阴影 / IBL 接回。

**产出物**：**可视化 Demo + 录屏 + 更新后的架构图**。简历第一条能写的东西。
**学习点**：数据契约如何真正被消费、GPU 资源生命周期与延迟销毁、frame-in-flight、命令录制、Backend 不回调 Runtime 的约束。
**常见坑**：① Backend 想回调 Runtime 拿数据（"就一个 getter"）→ 破坏解耦，改成快照字段；② 资源加载与帧循环竞争（上传时机）；③ GLFW / 窗口事件与 Runtime 输入系统的职责划分；④ 借来的旧代码 `ObjectDB` / `Ref` 与 YRuntime 版本冲突 → 统一到 `yr::render::vk` 命名空间。
**Godot 对照**：`servers/rendering/rendering_server_default.h`、`drivers/vulkan/`、`servers/display/display_server_wrapper.h`。
**降级方案**：不做双缓冲、场景只用 3 个立方体 + 一个相机。**"Runtime 驱动画面"这一条不能降级**，它是本阶段全部意义。

---

## M8 · 序列化与场景资源（3~4 周）★ 最难讲清也最出彩

**目标**：世界可以变成文件，文件可以变回世界，且**文件是人类可读可手写的**。

- [ ] `yr/serialize/writer.h` / `reader.h`：`IWriter`/`IReader` 抽象（结构化 begin/end + 错误位置报告）
- [ ] `TextWriter`/`TextReader`：自定义 `.yrscn`。**先把格式写下来再写解析器**（在对话里过一遍：段结构、引用怎么表达、错误怎么报），格式定稿后写进 `yr/serialize/` 的头注释
- [ ] `BinaryWriter`/`BinaryReader`：`.yrscnb`（magic + version + 长度前缀；处理对齐与字节序，写 `static_assert(sizeof(...))`）
- [ ] `JsonWriter`：调试导出（用 nlohmann_json，M8 起才引入 third_party）
- [ ] `VariantCodec`：Variant ↔ 文本/二进制（含 Vector3 等类型的字面量语法）
- [ ] 属性级序列化：遍历 `ClassInfo` 属性表，只序列化 `kSerialized` 且**与默认值不同**的属性（省空间 + diff 干净）
- [ ] 引用解析：ExtResource（外部资源，存 path/UID）/ SubResource（内嵌子资源）/ NodeRef（同场景节点，存 node index）
- [ ] `yr/scene/packed_scene.h`：`PackedScene`（场景的内存中间表示：节点数据表 + 父子关系 + 连接），`instantiate()` 生成真实 Node 树
- [ ] 版本兼容：`format_version` + 未知属性 warning 跳过 + 缺失属性用默认值 + migrator 链（写一个 v0→v1 的假迁移做测试）
- [ ] `tools/yr_scene_conv`：文本 ↔ 二进制互转 + `--validate`
- [ ] 测试：round-trip（save→load→save，两次输出**逐字节相同**）、手写文件加载、未知属性容忍、破坏性变更迁移、循环引用、深层嵌套
- [ ] 把 M4 的 headless 运行改成从 `.yrscn` 加载场景

**验收标准**
- **确定性 round-trip**：`yr_scene_conv a.yrscn → a.yrscnb → b.yrscn`，`diff a.yrscn b.yrscn` 为空；`save→load→save` 逐字节一致。（这条最难，也最能证明设计正确）
- 手写一个 20 行的 `.yrscn`（不经工具生成）能被正确加载。
- 加载一个含未知属性/未知类的文件 → 不崩溃，warnings 列表准确指出行号与内容。
- v0 文件经 migrator 后能被当前版本加载（测试）。
- 序列化 1000 节点树的耗时与文件大小记录进 benchmark 报告。

**产出物**：`yr_scene_conv` + round-trip 测试 + **格式规范**（写在 `yr/serialize/` 的头文件注释或 `assets/README.md` 里，不单独建文档）。**这是面试里最容易讲出深度的模块**（引用/版本/循环三大难题）。
**学习点**：格式设计、visitor 模式、token 解析（手写 lexer/parser，不用 lex/yacc）、二进制布局与对齐、版本迁移策略、确定性输出（保序容器、浮点格式化）。
**常见坑**：① 浮点数文本化精度丢失（用 `%.17g` 或直接存十六进制位模式）；② `Dict` 用 `unordered_map` 导致输出顺序不定 → §4.3 已规定用保序 vector；③ 节点 index 与父子关系混在一起导致解析要两遍（就是要两遍，别抗拒）；④ 字符串转义（`\n`、引号、UTF-8）；⑤ 文件路径平台分隔符。
**Godot 对照**：`scene/resources/resource_format_text.cpp/.h`（`.tscn` 的完整读写实现，**M8 精读，对照你的格式设计**）、`core/io/resource_format_binary.cpp`（`.res` 二进制 + 版本兼容处理）、`scene/resources/packed_scene.h/.cpp`（`PackedScene::instantiate` 的 node data / connection / edit state 三张表）、`tests/core/io/test_scenes.cpp`（Godot 怎么测场景存读档）、`core/variant/variant_parser.h`。
**降级方案**：二进制格式推到 M11（先只做文本）；`PackedScene` 先做扁平版（不支持内嵌子资源与场景嵌套实例化）；migrator 只留接口不实现链。

---

## M9 · 资源系统（2~3 周）★ 资源有身份、有引用计数、能异步、能释放

**目标**：资源有身份（UID）、有注册表、有引用计数、能异步加载不卡帧、能自动卸载；并用**正式 `AssetUID`/`AssetDatabase` 替换 M5 的临时内建 triangle/default material 注册表**，正式完成 `AssetHandle → GPU` 资源桥接。

- [ ] `yr/asset/asset_uid.h`：`AssetUID`（内容哈希或稳定 ID）+ `path ↔ uid` 映射表（对照 Godot `resource_uid.h`）
- [ ] `yr/asset/resource.h`：`Resource : RefCounted`（source_path / uid / 加载状态 / 元数据）
- [ ] `yr/asset/file_access.h`：`IFileAccess` 抽象 + `DiskFileAccess` + **`MemoryFileAccess`（测试用，见原则 P5）**
- [ ] `yr/asset/format_loader.h`：`IResourceFormatLoader` + 按扩展名分派注册表；实现 `.yrres`（M8 的二进制/文本资源）、`.json`、`.txt`、`.yrscn`
- [ ] `yr/asset/asset_database.h/.cpp`：`AssetDatabase`（同步 `load<T>`、异步 `request_load`、in-flight 合并、`pump()`、`collect_garbage()`、`AssetStats`）
- [ ] 异步加载：**先做单线程版**（`request_load` 只是入队，`pump()` 里同步执行），M10 再换成 job；这个"错的但能跑的"版本要记进 ADR
- [ ] 慢 IO 模拟：`ThrottledFileAccess`（每次读 sleep N ms），用来**证明异步加载不卡帧**（这是本里程碑的核心测试）
- [ ] 卸载：帧末 `collect_garbage()`（refcount==1 即只有 DB 持有）+ 手动 `evict(uid)` + 统计
- [ ] **AssetHandle 正式接入**：用真实 `AssetUID`/`AssetDatabase` 替换 M5 的临时注册表；Runtime 侧只发 upload/release 请求，Backend 侧按 generation 延迟销毁 GPU 资源
- [ ] 接到 `MainLoop` 阶段 4（`pump_assets`）与阶段 12（`collect_garbage`）
- [ ] 把 M8 的 ExtResource 解析接到 AssetDatabase（场景加载 = 递归资源加载，**注意加载顺序与循环依赖检测**）
- [ ] 测试：in-flight 合并（同一资源并发请求 10 次只加载一次）、失败路径（文件不存在/格式错误）、卸载后 handle 失效、慢 IO 下主循环帧时间不受影响

**验收标准**
- 用 `ThrottledFileAccess`（单次读 50ms）连续请求 20 个资源：主循环帧时间 p95 < 5ms，20 个资源在 ~1s 内全部就绪（异步生效）。
- 同一资源被 5 处引用 → 内存里只有一份（测试用 ObjectDB 计数验证）。
- 全部引用释放后下一帧 GC → 资源被卸载，`resident_count` 归零。
- 循环依赖（A 引用 B，B 引用 A）→ 检测到并报错，不死循环。
- `render_demo` 用真实 `AssetUID` 加载资源，临时 `AssetHandle` 注册表被删除。
- 所有测试用 `MemoryFileAccess`，**不碰真实磁盘**（可在 CI 跑）。

**产出物**：`AssetStats` 输出（驻留数/命中率/加载字节）。
**学习点**：异步状态机、future/promise vs 回调、引用计数与 GC 的边界、内容寻址、IO 抽象与依赖注入（测试友好设计）。
**常见坑**：① 在工作线程构造 Object → 违反 §6 铁律（M10 要特别小心）；② GC 时机与 `Ref` 临时对象（一个表达式里的临时 `Ref` 会让 refcount 短暂 >1，导致该帧不卸载 —— 这是正常行为，别"修"它）；③ 加载失败后 in-flight 条目没清理 → 永久卡住；④ 场景递归加载的深度与栈溢出。
**Godot 对照**：`core/io/resource.h`、`core/io/resource_loader.h/.cpp`（**精读 `ThreadLoadTask` 结构与 `load_threaded_request`/`load_threaded_get_status`，这是 M9+M10 的最佳参考**）、`core/io/resource_uid.h/.cpp`、`core/io/file_access.h` + `file_access_memory.h`（内存 FS）、`file_access_pack.h`（PCK，M11 用）、`tests/core/io/test_resource.cpp` + `test_resource_uid.cpp`。
**降级方案**：不做 UID（只用 path）；GC 只做手动 `evict`；异步只做"入队 + pump 执行"不做真线程。

---

## M10 · 任务系统（3~4 周）★★ 最容易翻车，务必按三步走

**目标**：把 M9 的"假异步"变成真并行，并让 Transform 更新、资源解码能跑在多核上——**同时不引入不可复现的 bug**。

- [ ] **M10-a 线程池**：`yr/job/job_system.h`：N 个 worker + `MtQueue`（吸收 yo_lib `yo_mtqueue.h` 并重构：加 `try_pop`、`stop()` 幂等、避免 busy wait）+ `submit` + `wait_all` + 优雅停机
- [ ] `TaskHandle`：`ready()` / `wait()`；主线程 `wait` 时**帮忙执行任务**而不是 sleep（对照 Godot）
- [ ] 把 `AssetDatabase` 的 IO+解码搬到 worker，构造仍回主线程（`pump()`）——**验证 §6 铁律**
- [ ] **M10-b 依赖图**：`submit(fn, deps)`（依赖计数 + 就绪队列）；`parallel_for(n, batch, fn)`
- [ ] Transform 层级并行刷新：按"脏根"分片，保证同一子树不并行（写清楚分片策略）
- [ ] **M10-c（可选）work stealing**：每线程本地双端队列 + 随机偷取；此时才深入 memory order
- [ ] `JobStats`：每线程独立 cacheline（`alignas(64)`）避免 false sharing；统计队列深度/等待/窃取次数
- [ ] **确定性保证**：并行任务的**结果**必须与串行一致（浮点求和顺序！`parallel_for` 里禁止做非结合性归约，或提供确定性归约接口）——写测试证明
- [ ] 线程安全审计：给所有"仅主线程"的 API 加 `YR_ASSERT(on_main_thread())`
- [ ] **内存序决策写进 `yr/job/` 的头文件注释**：哪个 atomic 用哪个 order、为什么。这类信息离开代码就会失效，不适合放文档
- [ ] benchmark：`benchmarks/bench_jobs.cpp`（1k/10k/100k 个任务的串行 vs 并行；不同 batch size 的曲线）
- [ ] TSan preset 跑全部测试 + `runtime_headless` 10000 帧

**验收标准**
- TSan 下全部测试 + 10000 帧集成测试**零报告**（这一条不过就不算完成）。
- `parallel_for` 结果与串行逐元素相同（含浮点，测试用固定种子数据）。
- benchmark：10k 个 100µs 任务，8 线程加速比 ≥5x（达不到要分析：任务粒度？队列锁？false sharing？分析写进报告）。
- 优雅停机：`~JobSystem()` 在有待处理任务时不死锁、不泄漏线程（ASan+TSan 干净）。
- 关闭 job（`--jobs 0`）后所有测试依然通过 —— **单线程路径必须始终可用**，这是可调试性的保命设计。

**产出物**：benchmark 报告 + 内存序决策表 + TSan 报告截图。
**学习点**：C++ 内存模型、`memory_order_relaxed/acquire/release/acq_rel/seq_cst` 的实际差别、false sharing、work stealing、任务粒度、`std::atomic_flag`/futex、条件变量的 lost wakeup。
**常见坑**：① 条件变量 `wait` 忘 predicate → lost wakeup 死锁；② `wait_all` 在 worker 线程调用 → 死锁（必须 assert）；③ 任务里捕获 `this` 而对象已析构 → 用 handle + 生存期保证；④ 析构顺序：JobSystem 必须在 AssetDatabase 之后销毁（否则任务回调访问死对象）—— 写进 `Engine::shutdown` 的顺序文档；⑤ 浮点归约顺序导致不可复现；⑥ 过度并行（任务太小，锁开销 > 收益）。
**Godot 对照**：`core/object/worker_thread_pool.h/.cpp`（**精读**：task group、`wait_for_task_completion`、主线程帮忙执行的逻辑）、`core/templates/command_queue_mt.h`（跨线程命令队列）、`core/io/resource_loader.cpp` 的 threaded load 部分（真实项目里"IO 并行 + 主线程收尾"的完整范例）、`servers/server_wrap_mt_common.h`（stretch 独立渲染线程会用）。
**降级方案**：只做 M10-a（线程池 + `wait_all`）；`parallel_for` 用 `std::async` 顶替；work stealing 直接砍掉列入 stretch。**M10 砍范围是完全可接受的**，确定性比性能重要。

---

## M11 · 工具链与打磨（2~4 周，可与 M8~M10 并行）

**目标**：让项目从"一堆 C++ 代码"变成"有开发流程的引擎环境"，并完成作品包装。

- [ ] `tools/yr_pack`：`assets/` → `.yrpak`（manifest(JSON/二进制) + blob 区 + 内容哈希 + 版本号）；对照 Godot PCK
- [ ] `PakFileAccess`：Runtime 从 `.yrpak` 加载；`--pak` 启动参数；验证 `runtime_headless` / `render_demo` 能完全从 pak 跑起来
- [ ] 资源导入管线雏形：`assets_src/`（源）→ `yr_pack` 转换/压缩 → `assets/`（cooked），写清"为什么需要 cook"
- [ ] 性能：接入 `perf` 工作流文档 + 可选 Tracy（submodule）；FrameStats 导出 CSV + 一个 Python 绘图脚本
- [ ] 调试文档：`.gdbinit`、常用断点配方、core dump 分析、ASan 报告解读、Catch2 `--break`
- [ ] （可选）clang-tidy：装 + `.clang-tidy` + CI 接入。**优先级最低**——它被 M0→M1→M11 连推三次，说明对你的学习价值最低；不痛就不做（R3）
- [ ] （可选）覆盖率：gcovr/lcov → 本地报告或 CI 徽章。**只统计，不做门禁**（学习项目里覆盖率是给别人看的数字，不是质量指标）
- [ ] 文档收尾：架构图更新（Mermaid）、`doc/README.md` 状态更新、README 重写（面向"第一次看到这个项目的人"）。**不在 doc/ 写"每模块一页说明"**——那些内容属于头文件注释（D17 文档要有限）
- [ ] `07-portfolio.md` 收口：展示物清单打勾、§5 面试题库自问自答一遍（**不设博客篇数指标**——想写就写，见 §4）
- [ ] （可选）ImGui 只读检视器：SceneTree + 属性 + FrameStats
- [ ] （可选）热重载：`.yrscn` 文件变更 → 自动重载场景（inotify），演示效果极好

**验收标准**
- `apps/render_demo --pak game.yrpak` 完全从 pak 启动运行，`assets/` 目录不存在也能跑。
- CI：build + ctest + format + 架构门禁全绿。（clang-tidy / 覆盖率若做了就加上，没做不阻塞）
- 一个完全陌生的开发者按 README 能在 15 分钟内构建并跑起两个可运行物（`runtime_headless` / `render_demo`）（**找人实测一次**）。
- `doc/` 与代码一致：架构图是最新的、`06-decisions.md` 收录了全部架构级取舍、`benchmarks/README.md` 有数据。

**产出物**：完整可展示仓库 + 工具（+ 你想写的博客）。
**Godot 对照**：`core/io/file_access_pack.h`（PCK 格式）、`editor/platform/`（导入管线）、`platform/web/`（打包思路）、`core/io/resource_format_binary.cpp`（cooked 格式）。
**降级方案**：Tracy、ImGui 检视器、热重载、clang-tidy、覆盖率全部可砍——M11 是收尾里程碑，不是必做清单。

---

## S · Stretch 清单（想法先进这里，不进当前里程碑）

| # | 想法 | 依赖 | 学习价值 | 备注 |
|---|---|---|---|---|
| S1 | 脚本绑定：嵌入 Lua 或自研极简 bytecode VM | M2 | ★★★★★ | 反射系统的终极检验；`03-learning-map.md` 有阅读材料 |
| S2 | `YR_BIND_METHOD` 方法绑定 + 从脚本调用 | M2 | ★★★★ | 对照 Godot `method_bind.h` |
| S3 | ECS 数据层：把 Component 数据搬进 SparseSet/Archetype，与 Object 树并存 | M4 | ★★★★★ | 对照 EnTT/flecs；写 benchmark 说明两种范式适用场景 |
| S4 | `IPhysicsBackend` + 玩具 AABB/扫掠实现 + 固定步长插值 | M4 | ★★★ | 理解"物理作为 Server" |
| S5 | 独立渲染线程 + command queue | M7 | ★★★★ | 对照 `server_wrap_mt_common.h` |
| S6 | 迷你 Frame Graph（pass 声明读写 → 自动 barrier） | M7 | ★★★★★ | Yo_Renderer roadmap §1.3 的延续 |
| S7 | 热重载（场景/资源/代码） | M11 | ★★★ | 演示效果极佳 |
| S8 | ImGui 编辑器（Hierarchy/Inspector 可编辑 + undo/redo） | M11 | ★★★ | 对照 `core/object/undo_redo.h` |
| S9 | 内存：自定义 allocator / pool / 分配统计 | M1 | ★★★ | 对照 `core/os/memory.h` |
| S10 | Profiler：内建采样 profiler + 火焰图导出 | M4 | ★★★★ | 对照 `core/debugger/engine_profiler.h` |
| S11 | 网络：确定性锁步或状态同步（用 headless simulation 做 demo） | M4 | ★★★ | 会倒逼"确定性"设计 |
| S12 | Windows 移植 | M7 | ★★ | 工程体检 |

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
- [ ] `07-portfolio.md` 更新（这个里程碑能往简历/博客里写什么）—— M7（首个视觉节点）之前可以跳过
- [ ] git tag：`v0.M?`
