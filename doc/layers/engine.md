# engine 层（`yr_engine` + apps 装配）

> 代码位置：`engine/engine/include/yr/engine/` 与 `src/` · 顶层装配与帧循环：`Phase`、`Clock`、`MainLoop`、`FrameStats`、`Engine`；`apps/runtime_headless` 是 headless 入口。
>
> 帧阶段的权威设计见 `../01-architecture.md` §5。本文件记录代码级契约与阶段接线现状。

## Phase（`engine::Phase` / `phaseName` / `kPhaseCount`）

- **职责**：帧阶段编号 0..13 的枚举与阶段名。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `enum class Phase : uint8_t` | `kBeginFrame=0 … kEndFrame=13`，`kCount=14` | — |
  | `constexpr size_t kPhaseCount` | `= static_cast<size_t>(Phase::kCount)` = 14 | — |
  | `const char* phaseName(Phase) noexcept` | 阶段名用于日志与测试探针 | 越界返回 `"unknown"` |

- **不变量 / 顺序**：编号固定为 0..13，**权威定义见 `../01-architecture.md` §5，任何改动都要写 ADR**。
- **Godot 对照**：`main/main.cpp` 的 `Main::iteration` 阶段划分（无同名枚举）。

## Clock（`engine::Clock`）

- **职责**：墙钟 delta 采样器。**只负责采样**：`timeScale` / `paused` 归 `MainLoop`，physics 累加器归 `MainLoop`。
- **所有权 / 生命周期**：不可拷贝 / 移动；`virtual ~Clock()` 以便测试派生。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `static constexpr double kDefaultMaxDelta = 0.25` | 默认单帧上限（秒） | — |
  | `explicit Clock(double maxDelta = kDefaultMaxDelta)` | 起点为构造时刻 | `maxDelta <= 0` 视为 0 |
  | `double nextDelta() noexcept` | 距上次调用的墙钟秒数 | 负值（时钟回拨）归零；超上限则截断 |
  | `void reset() noexcept` | 以当前 `now()` 为新起点，丢弃未采样的一段时间 | — |
  | `virtual TimePoint now() const noexcept`（protected） | 时间源注入点，默认 `steady_clock::now()` | — |

- **不变量 / 顺序**：**clamp 0.25s 发生在采样侧**（Clock 内部），防止调试断点 / 挂起后单帧巨大 delta 冲垮固定步长累加器；`tick(delta)` 是最终注入点、不经 Clock，故不受 clamp 影响。
- **设计原因**：clamp 是采样质量问题而非逻辑问题，因此放 Clock 侧；测试注入方式是派生并覆写 `now()`（脚本时间序列）或直接对 `MainLoop::tick(delta)` 传确定性值。
- **遗留 / 已知缺口**：无。

## MainLoop（`engine::MainLoop`）

- **职责**：**帧阶段序列的唯一权威实现**。一次 `tick` = 一次 0→13。
- **所有权 / 生命周期**：`SceneTree` / `EventBus` 由装配者拥有，`MainLoop` **引用非拥有**，二者必须比 `MainLoop` 活得更久。`RendererBackend*`（`setBackend`）同样非拥有，装配者拥有并保证其生命周期长于 `MainLoop`；未装配时阶段 11 为 no-op。不提供默认 `globalEventBus` 路径，避免隐藏的全局依赖。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `MainLoop(scene::SceneTree&, evt::EventBus&) noexcept` | 注入非拥有的树 / 总线；构造时标记"已接线阶段"的 per-phase 报告 | — |
  | `void tick(double realDelta)` | 执行 0→13 一次；`realDelta` 是本帧墙钟秒数，由调用方注入；**不读取 quit 标志** | — |
  | `uint64_t frameCount() const` | 已执行的 tick 数（`beginFrame` 累加） | — |
  | `uint64_t frameIndex() const` | 最近一次已执行帧的 0-based 序号；未 tick 时为 0 | — |
  | `bool shouldQuit() const` / `void requestQuit()` | 退出标志，由外层驱动 | — |
  | `double timeScale() const` / `void setTimeScale(double)` | 只影响 `gameDelta_`；负值 clamp 到 0 | — |
  | `bool isPaused() const` / `void setPaused(bool)` | 独立布尔 | — |
  | `double physicsStep() const` | 固定步长 `kPhysicsStep = 1/60` | — |
  | `double accumulator() const` | 当前未消费的物理累积（诊断用），spiral 截断后始终 `< physicsStep` | — |
  | `SceneTree& tree()` / `EventBus& events()` | 返回注入的引用 | — |
  | `void setBackend(render::RendererBackend*) noexcept` | 注入非拥有后端；`nullptr` 使阶段 11 回到 no-op | — |
  | `RenderSnapshot& snapshot()` / `const RenderSnapshot& snapshot() const` | 阶段 10 产出的当前帧快照；下一次 `tick` 才被覆写 | — |
  | `FrameStats& stats() const` | 帧统计；构造后、tick 前可配置 | — |

- **时间模型**：
  - `realDelta_` 在进入阶段 0 前暂存；`beginFrame` 里 `gameDelta_ = realDelta_ * effectiveTimeScale()`。
  - **`paused` 独立 bool**：`effectiveTimeScale() = paused_ ? 0.0 : timeScale_`，恢复时保留原 `timeScale_`。**没有第二处 paused 分支**——所有时间缩放只经 `effectiveTimeScale()`。
  - **累加器消费 `gameDelta_`**：`accumulator_ += gameDelta_`（`timeScale=0` 即暂停，物理 0 步）；阶段 7 的 `process` 也收 `gameDelta_`。
  - **固定步长 1/60，上限 8**：`while (accumulator_ >= kPhysicsStep && steps < kMaxPhysicsSteps) { onPhysicsStep(1/60); accumulator_ -= kPhysicsStep; ++steps; }`。
  - **spiral 截断 + warn-once**：达到上限后仍有积压时，`accumulator_ = fmod(accumulator_, kPhysicsStep)` 丢弃积压，并只报告一次（`spiralReported_`，避免卡顿时每帧刷屏）。
- **不变量 / 顺序**：15 个 `protected virtual` 方法（14 个阶段方法 0→13 + 阶段 6 内循环的 `onPhysicsStep`）便于测试探针记录顺序与后续扩展；默认空实现，接线时保持签名不变。`endFrame` 自己计时并汇总入环。
- **设计原因**：帧阶段的顺序即正确性，集中在一处实现，`Engine` 不复制阶段编排；`tick` 不检查 `shouldQuit`，退出决策留给外层（`../01-architecture.md` §5）。

### 14 阶段接线现状

| # | 阶段 | 方法 | 实现 | 调用子系统 / 安全点 | 本帧读写 |
|---|---|---|---|---|---|
| 0 | begin_frame | `beginFrame` | ✅ | — | `++frameIndex_`；算 `gameDelta_`；`accumulator_ += gameDelta_`；`physicsStepsFrame_ = 0` |
| 1 | poll_input | `pollInput` | ⬜ 空钩子 | 无 | — |
| 2 | flush_messages | `flushMessages` | ⬜ 空钩子 | 无 | — |
| 3 | flush_events | `flushEvents` | ✅ | `EventBus::flush`（安全点） | 派发上一帧 post 的事件 |
| 4 | pump_assets | `pumpAssets` | ⬜ 空钩子 | 无 | — |
| 5 | pre_physics | `prePhysics` | ⬜ 空钩子 | 无 | — |
| 6 | physics_step | `physicsSteps` | ✅ | `onPhysicsStep`（默认 no-op，可覆写） | 消费 `accumulator_`，写 `physicsStepsFrame_` |
| 7 | process | `processPhase` | ✅ | `SceneTree::process`（安全点） | 前序派发 `onUpdate` |
| 8 | post_process | `postProcess` | ✅ | `SceneTree::refreshTransforms`（安全点） | 刷新 Transform 缓存 |
| 9 | apply_structure | `applyStructure` | ✅ | `SceneTree::flushDeletions`（安全点） | 执行本帧延迟删除 |
| 10 | extract_render | `extractRender` | ✅ | `RenderExtractSystem::extract` | 写 `snapshot_`（`frame_index` 取 `frameIndex()`，全量前序抽取 `items`） |
| 11 | present | `present` | ✅ | `RendererBackend::present(snapshot_)`（非拥有后端） | 后端只读快照；未装配后端时 no-op |
| 12 | collect_garbage | `collectGarbage` | ⬜ 空钩子 | 无 | — |
| 13 | end_frame | `endFrame` | ✅ | `FrameStats::recordFrame` | 量本阶段耗时，汇总整帧 / 各阶段 / 物理步数入环 |

> 空钩子 = 1/2/4/5/12，尚未接任何子系统。per-phase 报告只覆盖已接线阶段（0/3/6/7/8/9/10/11/13）。

- **Godot 对照**：`main/main.cpp` 的 `Main::iteration`。
- **遗留 / 已知缺口**：空钩子见上表。

## RenderExtractSystem（`engine::RenderExtractSystem`）

- **职责**：`SceneTree → RenderSnapshot` 的**唯一单向转换点**（`../01-architecture.md` §4.11 规则② / D7）。M5 **全量抽取、不做剔除**。
- **所有权 / 生命周期**：无状态、无所有权；`extract` 只读 `SceneTree`、只写调用方传入的 `RenderSnapshot`。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `void extract(const scene::SceneTree&, render::RenderSnapshot& out) const` | 重置 `out.camera` 为固定默认（identity），按**前序**清空并填充 `out.items`；**不修改** `out.frame_index` | 空树安全（`items` 空，`camera` 仍填默认） |

- **抽取规则**：
  - 前序遍历整棵树（含 `root`），每个树内节点产出一个 `DrawItem`；顺序即前序，无排序键。
  - `DrawItem::object.value = node.id().raw()`；`DrawItem::world = node.globalTransform()`（均为 column-major，逐元素拷贝，不 memcpy 结构体）。
  - `DrawItem::mesh` / `material` 保持无效（`AssetHandle{}`）；M5 无资源系统，未就绪语义推迟到 M9。
  - `CameraPod` 的 `view` / `proj` 均 identity（`TransformPod` 默认即 identity）；真实投影留到 M7。
- **不变量 / 顺序**：每次 `extract` 先 `clear` 再填充，不累加；`items.size()` == 树内节点数（含 `root`）。`frame_index` 由调用方（`MainLoop::extractRender`）在本阶段写入。
- **设计原因**：把 Runtime↔Renderer 的耦合收敛到**一处、单向**，后端拿到的全是值。
- **Godot 对照**：无直接对应（Godot 走命令式 Server，见 §4.11）。

## FrameStats（`engine::FrameStats`）

- **职责**：帧耗时统计 —— 环形窗口保留最近若干帧的 total 与各阶段耗时，按间隔打印，并可 CSV 导出。
- **所有权 / 生命周期**：仅持有 `std::vector` / `std::ofstream`，无外部指针；不可拷贝 / 移动；析构时若 CSV 仍开启则关闭文件。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `struct FrameStatsConfig { size_t windowSize = 4096; uint64_t reportInterval = 1000; }` | 窗口同时是报告滑动窗口；`reportInterval = 0` 表示只记录不打印 | `windowSize == 0` 视为 1 |
  | `explicit FrameStats(FrameStatsConfig = {})` | — | — |
  | `void recordFrame(Duration total, const array<Duration, kPhaseCount>&, int physicsSteps)` | 汇总点：本帧入环、按间隔报告、必要时写 CSV；由 `MainLoop::endFrame` 调用 | — |
  | `void setPhaseReported(Phase, bool)` / `bool isPhaseReported(Phase) const` | 是否在周期报告输出该阶段 per-phase 百分位；默认全关，由 `MainLoop` 只标记真正接线的阶段 | 越界索引忽略 / 返回 `false` |
  | `void setReportInterval(uint64_t)` / `uint64_t reportInterval() const` | 报告间隔（帧） | — |
  | `bool enableCsv(const std::string&)` | 打开 CSV 并写 header；重复调用先关旧文件 | 路径打不开 / 空路径 → `false` + 日志 |
  | `void finish()` | 关闭 CSV；幂等；析构自动调用 | — |
  | `uint64_t frameCount() const` | 自构造以来帧数 | — |
  | `int totalPhysicsSteps() const` | 累计 physics 步数 | — |
  | `size_t windowCount() const` / `windowSize() const` | 当前窗口样本数（`<= windowSize`）/ 窗口大小 | — |
  | `Duration totalP50/P95/Max() const` | 整帧百分位 | — |
  | `Duration phaseP50/P95/Max(Phase) const` | 阶段百分位 | 越界返回 0 |

- **不变量 / 顺序**：
  - **逐阶段采样**：`MainLoop::tick` 用 `ScopeTimer`（`phaseTimer_.restart()`）测量 14 个阶段，存入 `phaseDurations_`；整帧用 `frameTimer_`。
  - **环形窗口 4096**（默认）：`window_` 定长，`writeIndex_` 回绕；`sampleCount_` 最大到 `windowSize_`。
  - **报告间隔默认 1000**（`0` = 静默）：`frameCount_ % reportInterval_ == 0` 时 `report()`。
  - **报告内容**：`total` 打 **p50/p95/max**；各阶段**只打 p50**（`report()` 只对 `reportPhase_` 为真的阶段输出 `p50`）。
  - **CSV 可选**：`enableCsv` 后立即写 header `frame,total_ns,physics_steps`，之后每帧追加一行。
  - 百分位算法：最近秩，升序排序后 `index = ceil(fraction * n) - 1`。
- **设计原因**：只对已接线阶段打 per-phase，避免日志里全是 0ns 噪音；阶段只打 p50 以控制日志长度（total 才需要尾部信息）。
- **Godot 对照**：`core/object/performance.h`（Performance 单例，指标口径不同）。
- **遗留 / 已知缺口**：CSV 只含 total + physics 步数，不含 per-phase；无逐线程槽（计划中的多线程统计未实现）。

## Engine（`engine::Engine`）

- **职责**：headless 运行时的顶层装配，组合拥有各子系统并驱动 while loop。
- **所有权 / 生命周期**：**组合拥有顺序** `sceneTree_ → eventBus_ → clock_ → mainLoop_`，按声明**逆序**析构，保证 `mainLoop_` 最先析构、`sceneTree_` 最后析构。`mainLoop_` 构造时注入前两者且不拥有它们。**`Engine` 不拥有、不创建 `RendererBackend`**（`yr_engine` 不得依赖 `yr_render_null`，由 `tools/check_deps.py` 保证）；装配者创建并保证其生命周期长于 `Engine`。不可拷贝 / 移动。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `Engine() noexcept` | `mainLoop_(sceneTree_, eventBus_)` | — |
  | `void run(uint64_t maxFrames)` | 最多运行 `maxFrames` 帧：每帧 `mainLoop_.tick(clock_.nextDelta())`，达到上限或 `shouldQuit()` 后退出 | 使用真实 Clock，无 delta 确定性保证 |
  | `void setBackend(render::RendererBackend*) noexcept` | 转发 `mainLoop_.setBackend`；非拥有 | — |
  | `SceneTree& sceneTree()` / `EventBus& events()` / `MainLoop& mainLoop()` | 暴露子系统 | — |

- **不变量 / 顺序**：**帧阶段顺序唯一定义在 `MainLoop`，`Engine` 不复制阶段编排**。确定性测试直接用 `MainLoop::tick(fixedDelta)`。
- **设计原因**：用成员声明顺序控制析构顺序，替代手写 `initialize` / `shutdown`（`01-architecture` §4.12）。`EngineConfig`、全局单例、Asset/Job/Renderer 装配与显式启停顺序推迟到确有消费者时再加。
- **遗留 / 已知缺口**：无子系统的显式 `initialize` / `shutdown`；无 render / asset / job 装配。

## apps/runtime_headless（`apps/runtime_headless`）

- **职责**：headless 正式运行入口。解析参数、装配 `Engine`、跑 N 帧并报告。
- **公开行为**（`main.cpp`）：

  | 参数 / 行为 | 契约 |
  |---|---|
  | `--frames N` | 帧数，默认 **1000**；用 `std::from_chars` 解析，非数字 / 越界拒绝 |
  | `--help` / `-h` | 打印 usage 到 stdout，退出码 0 |
  | 非法参数 | usage 到 stderr，退出码 2 |
  | 运行 | `auto backend = create_null_backend(); engine.setBackend(backend.get()); engine.run(options.frames);`（backend 由入口拥有，生命周期覆盖 `run`） |
  | 结束打印 | `frames=…`、`physics_steps=…`（`stats.totalPhysicsSteps()`）、`alive=…`（`globalObjectDB().aliveCount()`）、`items=/draw_calls=/triangles=`（`backend->last_frame_stats()`）、`total p50/p95/max` |

- **不变量 / 顺序**：不生成场景或演示节点；装配由 `Engine` 负责；backend 在 `run` 前创建、`run` 返回后才读取统计。
- **设计原因**：手写参数解析只支持帧数与 help，不引入 CLI 库。
- **遗留 / 已知缺口**：不生成场景（`items` 恒为 0，快照稳定性由 `tests/integration` 覆盖）；无 asset / job 装配；无 `--frames 0` 以外的其他开关。