# M10 最后收尾：内存序头注释 + 确定性回归

> 状态：进行中（M10-a / M10-b / JobStats / TSan preset / bench_jobs 已完成）
> 最近更新：2026-11-19

## 1. 目标

两件小事收掉 M10：

1. **内存序决策写进 `engine/job/include/yr/job/job_system.h` 的头注释**（用户手写）；
2. **确定性回归测试**（AI 写）：`parallel_for` 两轮运行、不同 batch size 的结果逐元素一致，并与串行计算相同。

## 2. 为什么写进头文件而不是文档

`doc/layers/job.md` §8 已有一份整理，但 roadmap 的原话是"这类信息离开代码就会失效"。三个月后读代码的人（包括你自己）只会打开 `.h`，所以**每个 atomic / 计数器的同步理由必须写在声明旁边**，文档只做索引。

## 3. 头注释要覆盖的内容（对照现状）

至少写清下面几条，逐条对应代码：

| 符号 | 需要说明的决策 |
|---|---|
| `workerCount_` | 为什么 `std::atomic<std::size_t>`；为什么取默认 `seq_cst`（简化正确性，不需要手写 acquire/release） |
| `pendings_` / `submitted_` / `completed_` / `helperCompleted_` / `peakReadyDepth_` | 普通 `size_t`，**全部访问都在 `mutex_` 内**；不要改成 atomic（会破坏"检查+动作同一临界区"） |
| `cv_` / `mutex_` | 谓词（`pendings_` / `queue_.size()` / `state->completed`）必须持同一把 `mutex_`；`notify_all` 放锁外只是少一次争锁 |
| `TaskHandle::State::completed` | `std::atomic<bool>` 只服务锁外 `ready()`；**写侧仍在 `mutex_` 内**（与等待谓词同锁），所以读侧看到 true 之后的可见性由 mutex 传递 |
| `MtQueue` 内部 | `closed_` 是 atomic（`closed()` 声明 `noexcept` 不能加锁）；其余状态在队列自己的锁内 |
| 锁顺序 | `JobSystem::mutex_` → `MtQueue` 内部锁，不得反向 |
| 全局 | 当前没有手写 `acquire` / `release` / `acq_rel` 配对；如果将来做 work stealing（S13），再逐处论证 |

**交付物**：`job_system.h` 顶部的注释块（可以放在 `namespace yr::job {` 之前，或在每个成员声明旁加短注释，二选一）。不要改动任何代码语义。

## 4. 确定性回归（AI 已写好，当前应为绿）

`tests/job/test_parallel_for.cpp` 新增 2 条：

1. **two runs produce identical results**：`count = 4096`，纯函数 `f(i)` 写入数组；两次 `parallelFor` 结果逐元素相同，且等于串行循环；
2. **batch size does not change results**：同一输入用 batch = 1 / 7 / 64 / 4096 各跑一次，结果数组全部相同。

这两条锁的是"批划分只是执行策略，不改变语义"。它们**不**覆盖浮点归约顺序——因为 `parallel_for` 明确不提供跨批次归约（调用方负责）。

## 5. 明确不做

- `YR_ASSERT(on_main_thread())` 线程安全审计：**推迟**到 asset worker 迁移。当前没有跨线程访问 scene/asset 的消费者，现在加只是无消费者的负担；迁移时必须先加这个断言再动代码。
- work stealing（S13）、per-worker cacheline 统计（E12）。

## 6. 文件所有权

**用户手写：**

```text
engine/job/include/yr/job/job_system.h   （只加注释，不改语义）
```

**AI 负责：**

```text
tests/job/test_parallel_for.cpp
doc/tasks/**
```

## 7. 验收

```bash
cmake --build --preset debug-vk --target YrTests_job
./build/debug-vk/bin/YrTests_job
./tools/check_format.sh
python3 tools/check_deps.py -v
```

要求：

- 头注释覆盖 §3 的每一条；
- 50 + 2 = 52 条 `[job]` 用例全绿；
- 代码零语义改动（`git diff` 只应出现注释）。

## 8. Q&A / 决策记录

- 2026-11-19：内存序信息放头文件（roadmap 要求），`doc/layers/job.md` §8 保留为索引与扩展说明。
- 2026-11-19：确定性只保证"批次划分不改变结果"，不提供浮点归约接口。