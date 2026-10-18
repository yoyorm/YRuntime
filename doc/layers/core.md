# core 层（`yr_core`）

> 代码位置：`engine/core/include/yr/core/` · 无外部依赖的基础件：句柄、容器、字符串驻留、日志、断言、时间、版本。
>
> 本文件记录代码级 API 契约与"为什么"。设计意图与分层理由见 `../01-architecture.md` §4.1、§9。

## Handle（`core::Handle<T>`）

- **职责**：把 `index + generation` 打包成 64-bit 值语义弱引用，用于识别 `SlotMap` 的槽及其代际。
- **所有权 / 生命周期**：不拥有元素、不延长生命周期。元素由 `SlotMap` 独占；槽被复用（generation 前进）后旧句柄自动失效。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `constexpr Handle() noexcept` | 默认即 invalid（`raw_ == 0`） | — |
  | `bool valid() const` | `raw_ != 0` | — |
  | `uint32_t index() const` | 低位 24 bit | — |
  | `uint64_t generation() const` | 高位 40 bit | — |
  | `uint64_t raw() const` | 原始 64-bit 编码 | — |
  | `static Handle invalid()` | 返回默认构造 | — |
  | `static Handle fromRaw(uint64_t)` | 从裸值还原，**不做合法性校验** | 任意非零值都是 valid 句柄 |
  | `operator== / !=` | 按 raw 比较 | — |
  | `static Handle make(index, generation)`（private，friend `SlotMap`） | 规范编码 | — |

- **不变量 / 顺序**：`HandleBits::encode` 先把 generation 掩码到 40 位，使 `make()` 与 `SlotMap` 存储的 generation 落在同一取值域；`nextGeneration` 回绕到 0 时跳到 1（0 保留给 invalid）。
- **设计原因**：ABA 防护的最低成本实现——销毁只前进 generation，旧句柄自然失效；40 bit 回绕不可达（`01-architecture` §4.1）。
- **Godot 对照**：`core/templates/rid.h`、`core/templates/rid_owner.h`、`core/object/object_id.h`。
- **遗留 / 已知缺口**：规划中的 `Handle<void>` 与跨类型 `Handle<T>::from_void()` 未实现；`fromRaw` 对非法值不设防。

## SlotMap（`core::SlotMap<T>`）

- **职责**：稠密数组 + 空闲链 + `slot↔dense` 双向映射，O(1) 增 / 删 / 查，迭代按稠密顺序。
- **所有权 / 生命周期**：独占其元素；元素以值语义存储（`std::vector<T>`），`erase` 后元素被析构 / 移出。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `Handle<T> insert(T value)` | 从 freelist 取槽（复用槽前进 generation）或新增槽，返回新句柄 | 槽数超 24 bit → Debug assert |
  | `bool erase(const Handle<T>&)` | 归还槽到 freelist 使原句柄立即失效（generation 在下次 insert 复用时才前进） | 句柄已失效返回 `false` |
  | `bool contains(const Handle<T>&) const` | 校验槽在界且 generation 匹配 | — |
  | `T* get(const Handle<T>&)` / `const T*` | 失效返回 `nullptr` | — |
  | `T& operator[](const Handle<T>&)` | — | 失效 = Debug assert，Release 下 UB |
  | `Handle<T> handleAt(uint32_t pos) const` | 由稠密下标取句柄 | `pos` 越界 = Debug assert |
  | `size() / empty() / capacity() / data() / begin() / end() const` | 稠密视图 | — |
  | `reserve(size_t)` | 预留四个并行数组与 freelist | — |
  | `clear()` | 前进所有槽 generation 使旧句柄失效，清空稠密 | — |

- **不变量 / 顺序**：`slot2dense_` 用 `kInvalidDense` 标记空槽；`generation_` 从 1 起；`next_slot_` 单调增长，`clear()` **不重置**它。
- **设计原因**：`erase` 用尾部元素搬移而非 swap（少一次 move）；迭代只提供 `const_iterator`，不提供"迭代中安全删除"——那是 `SceneTree` 的职责（`01-architecture` §4.1）。
- **Godot 对照**：`core/templates/rid_owner.h`（slot map + 空闲链）。
- **遗留 / 已知缺口**：迭代中删除不安全（由上层负责）；`insert` 的 24-bit 溢出只 assert，无降级路径。

## ObjectID（`core::ObjectID`）

- **职责**：全局对象身份 tag，`index(24) + generation(40)`，跨模块传递。
- **所有权 / 生命周期**：不拥有对象、不负责生命周期。有效 ID 由 `ObjectDB` 创建；对象销毁后 `ObjectDB` 前进 generation 使 ID 失效。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `constexpr ObjectID() noexcept` | 默认 invalid（`raw_ == 0`） | — |
  | `valid() / index() / generation() / raw() const` | 位访问 | — |
  | `static ObjectID invalid()` | 默认构造 | — |
  | `operator==`（defaulted） | 按 raw 比较 | — |
  | `static ObjectID make(...)`（private，friend `ObjectDB`） | 规范编码 | 外部无法伪造 ID |

- **不变量 / 顺序**：与 `Handle` 相同的编码 / 回绕规则（`ObjectIDBits` 独立定义，位布局一致）。`ObjectID` 与 `Handle<T>` 是两套东西：前者走全局 `ObjectDB`，后者走各表内部索引（`01-architecture` §4.1 决策点）。
- **设计原因**：`ObjectDB`/`WeakRef`/延迟调用需要"全局唯一、可跨表解析"的身份，与表内句柄用途不同。
- **Godot 对照**：`core/object/object_id.h`。
- **遗留 / 已知缺口**：无（`ObjectDB` 侧见 `object.md`）。

## StringId（`core::StringId`）与 StringInterner

- **职责**：`StringId` 是指向驻留字符串节点的稳定标识，相等 / 哈希 O(1)。`StringInterner` 负责驻留，相同内容映射到同一节点。
- **所有权 / 生命周期**：`StringId` 非拥有（不延长驻留节点寿命）；`StringInterner` 独占节点，节点地址稳定（`std::list` 存储）。**只在当前进程地址空间内有效**。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `StringId::valid() const` | 节点非空 | — |
  | `StringId::view() const` | 无效 ID 返回空 view | — |
  | `StringId::invalid()` | 默认构造（`node_ == nullptr`） | — |
  | `StringId::operator==`（defaulted） | 指针比较 | — |
  | `StringInterner::intern(string_view)` | 相同字符串返回相同 ID；新串分配新 ID | — |
  | `StringInterner::lookup(StringId) const` | 无效返回空 view | — |
  | `StringInterner::size() const` | 当前驻留数量 | — |
  | `globalStringInterner()` | 进程级默认驻留表 | — |

- **不变量 / 顺序**：`StringInterner` 只追加、不删除；不可拷贝 / 移动；构造私有（仅 `globalStringInterner` 是 friend）。
- **设计原因**：`std::list` 保证节点地址稳定，`unordered_map<string_view, const StringNode*>` 复用节点自身的字符串做键，避免二次分配。
- **Godot 对照**：`core/string/string_name.h` / `.cpp`。
- **遗留 / 已知缺口**：`StringId` 无 `operator<`（这也是 `Dict` 用保序 `vector` 而非 `std::map` 的原因之一，见 `object.md`）；无删除 / 收缩接口。

## Timer / ScopeTimer / Duration（`core`）

- **职责**：`Duration = std::chrono::nanoseconds`、`TimePoint = steady_clock::time_point`；`ScopeTimer` 是单次区间计时器。
- **所有权 / 生命周期**：纯值类型，无堆分配；`ScopeTimer` 不可拷贝 / 移动。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `ScopeTimer() noexcept` | 以构造时刻为起点 | — |
  | `Duration elapsed() const` | 起点到现在的耗时 | — |
  | `Duration restart()` | 返回上一段耗时并以现在为新起点 | — |

- **设计原因**：单调 `steady_clock`，不受墙钟回拨影响；`restart()` 让同一计时器连续测量相邻区间（`MainLoop` 逐阶段采样即用此）。
- **遗留 / 已知缺口**：无。

## Log（`core`）

- **职责**：分级日志入口 + 可替换 handler + 运行期阈值过滤。
- **所有权 / 生命周期**：`LogInfo.message` 是 `string_view`，**不拥有数据，仅在 `reportLog` 返回前有效**。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `YR_LOG_DEBUG/INFO/WARN/ERROR/FATAL(tag, fmt)` | 宏；`DEBUG` 在 release 下整条剔除 | 格式化参数不转发（见遗留） |
  | `LogLevel minLogLevel() / setMinLogLevel(LogLevel)` | 运行期过滤阈值，低于它直接丢弃 | — |
  | `defaultMinLogLevel()` | 由 `YR_ENABLE_DEBUG_LOG` 决定的默认阈值 | — |
  | `defaultLogHandler(const LogInfo&) noexcept` | 打印到 stderr | — |
  | `setLogHandler(LogHandler)` | 传 `nullptr` 恢复默认 | — |
  | `reportLog(...) noexcept` | 过滤 → 组装 `LogInfo` → 调当前 handler | — |

- **不变量 / 顺序**：**handler 在日志锁内执行，handler 内不得再调用任何日志 API**（否则死锁）。`YR_ENABLE_LOG` 默认 1 且**不由 `NDEBUG` 推导**：日志 release 下也保留（`../05-engineering.md` §5）。
- **设计原因**：日志是诊断基础设施，release 也要在；只有 DEBUG 级整条剔除以省开销。
- **Godot 对照**：`core/error/error_macros.h`（打印宏）、`core/string/print_string.h`。
- **遗留 / 已知缺口**：fmt 风格格式化未实现，目前只传格式串本身（不转发 `__VA_ARGS__`）；`defaultLogHandler` 的时间戳 / 帧号 / 线程 ID 是 TODO(M0)。

## Assert（`core`）

- **职责**：可替换 handler 的断言入口，默认打印到 stderr 并中止。
- **所有权 / 生命周期**：`AssertInfo` 只持有裸字符串指针（字面量 / 宏文本），不拥有数据。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `YR_ASSERT(cond)` / `YR_ASSERT_MSG(cond, msg)` | 失败时调 `reportAssert` | Release（`YR_ENABLE_ASSERTS == 0`）下编译为空 |
  | `YR_VERIFY(cond)` | Debug 等同 `YR_ASSERT`；Release 下**仍求值**，只不报告 | — |
  | `YR_BREAKPOINT()` | `__builtin_trap()` | — |
  | `defaultAssertHandler(const AssertInfo&) noexcept` | 打印并返回 `kAbort` | — |
  | `setAssertHandler(AssertHandler)` | 传 `nullptr` 恢复默认 | — |
  | `reportAssert(const AssertInfo&) noexcept` | 取当前 handler → 调用 → `kAbort` 则 trap | — |

- **不变量 / 顺序**：`reportAssert` 用 `g_handling` 哨兵拒绝重入（重入即 trap），**非线程安全**；`YR_ENABLE_ASSERTS` 由 `NDEBUG` 推导。
- **设计原因**：handler 可替换以便测试捕获断言；`YR_VERIFY` 让"必须求值的检查"在 release 下不被删。
- **Godot 对照**：`core/error/error_macros.h`、`core/error/crash_handler.h`。
- **遗留 / 已知缺口**：`g_handling` 与 `g_handler` 无锁保护，仅在单线程语义下成立。

## 版本（`core`）

- **职责**：暴露构建期注入的版本串与配置名。
- **公开 API**：`versionString()`（`YR_VERSION_STRING`）、`buildConfig()`（`YR_BUILD_CONFIG`），均由 CMake 注入。
- **遗留 / 已知缺口**：无。