# M10 收口件：手写 `JobStats`

> 状态：**已完成**（2026-11-19：`job_stats` 6/6；`[job]` 50 cases / 2794 assertions；300 轮 0 失败；全量 554/554（debug 与 TSan）；ASan 干净）
> 最近更新：2026-11-19

## 1. 目标

给 `JobSystem` 增加一个运行统计快照，用于 benchmark 解读与后续 asset worker 迁移时的可观测性：

```cpp
namespace yr::job {

struct JobStats {
  std::size_t submitted = 0;        // 累计 submit 的任务数（含内联执行、等待依赖未就绪的）
  std::size_t completed = 0;        // 累计完成数
  std::size_t helperCompleted = 0;  // 其中由非 worker 线程执行的（内联 submit / waitAll / wait 帮忙）
  std::size_t peakReadyDepth = 0;   // 就绪队列深度峰值
  std::size_t readyDepth = 0;       // 当前就绪队列深度（近似值）
};

class JobSystem {
public:
  [[nodiscard]] JobStats stats() const;
};

} // namespace yr::job
```

## 2. 语义

- **`submitted`**：每次 `submit`（两个重载、含依赖未就绪的任务）都 +1；`workerCount == 0` 的内联路径同样 +1。
- **`completed`**：每个任务体执行完（`finishTask_` 完成路径）都 +1；内联路径的完成也在其内。
- **`helperCompleted`**：`completed` 的子集——任务体由**非 worker** 线程执行的次数。包括：
  - `workerCount == 0` 时调用线程的内联执行；
  - `waitAll()` 帮忙执行；
  - `TaskHandle::wait()`（`waitFor`）帮忙执行。
- **`peakReadyDepth`**：就绪队列（`queue_`）深度出现过的最大值。只统计"可以立即执行"的任务，不含等待依赖的任务。
- **`readyDepth`**：调用 `stats()` 那一刻的队列深度。
- **不是精确同步的实时值**：`stats()` 返回的是某一时刻的快照，允许与真实值有微小偏差；调用方不应把它当作同步原语。
- **不要求跨线程调用 `stats()`**：本阶段只保证主线程（非 worker）在 `waitAll()` 之后读取。

## 3. 明确不做

- per-worker `alignas(64)` 独立计数器 / false sharing 实验——没有真实消费者，E12 需要时再做；
- 任务耗时直方图 / percentile；
- 等待时间、唤醒次数、取消次数；
- 把统计接入 `FrameStats` 或渲染 demo。

## 4. 实现提示（由你决定取舍）

1. **计数位置**：
   - `submitted_++`：`submit` 的两个分支都算，位置在锁内、判就绪之前；
   - `completed_++`：`finishTask_` 的完成临界区里（和 `pendings_--` 同一把锁）；
   - `helperCompleted_++`：`finishTask_` 需要知道"是不是帮忙路径"。可选做法：给 `finishTask_` 加一个 `bool helped` 参数（worker 传 false，`waitAll` / `waitFor` 传 true），或者拆出 `finishTaskHelped_` 薄封装；
   - 内联路径：直接 `submitted_ / completed_ / helperCompleted_` 各 +1。
2. **`peakReadyDepth`**：队列只在两处 `push`——`submit`（依赖已满足）与 `finishTask_`（触发后继）。两处都在 `JobSystem::mutex_` 内，push 之后读一次 `queue_.size()` 并更新峰值即可（锁顺序 `mutex_` → `MtQueue` 内部锁，允许）。
3. **`readyDepth`**：`stats()` 里持 `mutex_` 后读 `queue_.size()`（`size()` 是加锁近似值）。
4. **`stats()` 的 `const`**：`JobStats stats() const` 需要 `mutable std::mutex mutex_;` 才能在 const 方法里加锁；如果你不想引入 `mutable`，把方法改成非 const 并在任务卡里记录这个取舍。
5. **内联模式的并发**：`workerCount == 0` 时若多个线程同时 `submit`，普通计数器会竞争。可以接受"内联模式只允许单线程调用"的契约，或在内联分支也短暂加锁——二选一，写进注释。

## 5. 文件所有权

**用户手写（AI 不得改实现）：**

```text
engine/job/include/yr/job/job_system.h
engine/job/src/job_system.cpp
```

**AI 负责：**

```text
tests/job/test_job_stats.cpp
tests/job/CMakeLists.txt
doc/tasks/**
```

> 红灯规则：占位声明与失败测试**不提交**，实现通过后与实现一起提交。

## 6. 测试

`tests/job/test_job_stats.cpp`（6 条，已全绿）：

1. **counts submitted and completed tasks**：4 worker / 200 任务，`waitAll()` 后 `submitted == 200`、`completed == 200`、`readyDepth == 0`、`helperCompleted <= completed`。
2. **zero workers count inline completions as helper**：`JobSystem(0)` / 16 任务，`submitted == 16`、`completed == 16`、`helperCompleted == 16`。
3. **helper completions are counted when the main thread waits**：1 worker 被 blocker 占住，队列里 1 个任务；主线程 `waitAll()` → `completed == 2`、`helperCompleted >= 1`。
4. **peak ready depth reflects queued backlog**：1 worker 被 blocker 占住；提交 5 个短任务 → `stats().peakReadyDepth >= 5`；释放后 `waitAll()`，`readyDepth == 0`。
5. **dependency chains are counted once per task**：`A → B → C` 任务链；`waitAll()` 后 `submitted == 3`、`completed == 3`。
6. **two job systems keep independent counters**：两个 `JobSystem` 分别提交不同数量任务，各自的 `submitted / completed` 互不影响。

## 7. 验收

```bash
cmake --build --preset debug-vk --target YrTests_job
./build/debug-vk/bin/YrTests_job
ctest --test-dir build/debug-vk -R "job_stats"
cmake --build --preset asan --target YrTests_job && ./build/asan/bin/YrTests_job
cmake --build --preset tsan && ./tools/ctest_tsan.sh -R "job_stats"
```

要求：

- 6 条 `job_stats` 用例通过；
- 现有 44 条 `[job]` 用例不回归；
- ASan / TSan 干净；
- `check_format.sh`、`check_deps.py` 通过。

## 8. Q&A / 决策记录

- 2026-11-19：统计范围收窄为互斥量保护的累计计数，不做 per-worker cacheline 计数器——当前没有读出这些数据的消费者，`alignas(64)` 的 false sharing 实验留到 E12。
- 2026-11-19：`stats()` 只保证非 worker 读取；不承诺"精确实时"，避免为了统计引入额外同步热点。
- 2026-11-19：`stats()` 保持 `const`，为此 `mutex_` 改为 `mutable`；const 查询加锁是常见取舍（读侧同步与接口 const 性二选一）。
- 2026-11-19：**帮忙路径的归类用参数而不是另设计数函数**：`finishTask_(Job, bool helped)`——worker 传 `false`，`waitAll` / `waitFor` 传 `true`；内联 `submit` 直接 `completed_/helperCompleted_` 各 +1（并在锁内完成，顺带覆盖了 `JobSystem(0)` 多线程提交的契约）。
- 2026-11-19：峰值只在两处 `push` 后更新（`submit` 依赖已满足、`finishTask_` 触发后继），且都持 `mutex_`，锁顺序 `mutex_` → `MtQueue` 内部锁不变；`readyDepth` 在 `stats()` 内持锁读 `queue_.size()`。
- 2026-11-19：内联分支在 `task()` 前后各加一次锁（中间释放），因此任务内递归 `submit` 不会自锁。- 2026-11-19：计数器用**普通 `std::size_t` + `mutex_`**，不用 `atomic`：`stats()` 是快照接口，且读点本来就要加锁；用 atomic 只会让"提交时一次原子加 + 完成时一次原子加"成为每个任务的热路径开销。代价：`stats()` 会与任务提交/完成抢同一把锁——按契约只在同步点读取，可接受。
- 2026-11-19：`submitted_++` 放在 `submit(task, deps)` 的锁内、判就绪之前（两个重载共用同一条实现）；内联分支单独用两个短临界区计 `submitted_` 与 `completed_/helperCompleted_`。
- 2026-11-19：`completed_++` 与 `helperCompleted_++` 在 `finishTask_` 的完成临界区里（与 `pendings_--` 同一把锁）；"是不是帮忙"由新增参数 `helped` 传入：worker 传 `false`，`waitAll()` 与 `waitFor()` 传 `true`，内联路径直接计 `helperCompleted_`。
- 2026-11-19：`peakReadyDepth` 只在两处 `push` 之后更新（`submit` 的"已就绪"分支、`finishTask_` 触发后继），统一走私有 `noteReadyDepthLocked()`，函数名后缀表明"必须持 `mutex_`"。锁顺序保持 `mutex_` → `MtQueue` 内部锁。
- 2026-11-19：`stats()` 保持 `const`，因此 `mutex_` 加 `mutable`；`readyDepth` 在持锁时读 `queue_.size()`（近似值）。
- 2026-11-19：验证：`job_stats` 6/6（23 assertions）；`[job]` 50 cases / 2794 assertions；debug 300 轮 0 失败；全量 ctest 554/554；ASan 干净；**TSan preset 全量 554/554**、`-R job` 26/26。
