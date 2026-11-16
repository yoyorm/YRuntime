# M10-a-5：TaskHandle::wait 主线程帮忙执行

> 状态：进行中（AI 已准备失败测试；实现由用户完成）
> 最近更新：2026-11-16

## 1. 目标

让 `TaskHandle::wait()` 在目标任务尚未完成（`!ready()`）时**主动帮忙执行队列中的任务**，直到**该句柄对应的任务完成**即返回（不必等待整个 `JobSystem` 的所有无关慢任务完成）。

## 2. 为什么

`waitAll()` 解决的是"等待全局队列清空"；但调用方经常只关心某一个子任务（或某一批任务的 `TaskHandle`）。
如果 1 个 worker 被任务 A 占住，而后续入队的任务 B（可能是解锁 A 的前置/协作任务，也可能是主线程正要等待的目标任务）还在队列里，`handle.wait()` 只在 `state_->cv` 上死等同样会造成阻塞或死锁。对照 Godot `WorkerThreadPool::wait_for_task_completion`，单任务等待同样需要主线程帮忙执行。

## 3. 文件所有权

**用户手写（AI 不得改实现）：**

```text
engine/job/include/yr/job/job_system.h
engine/job/src/job_system.cpp
```

**AI 负责：**

```text
tests/job/test_handle_wait_help.cpp
tests/job/CMakeLists.txt
doc/tasks/**
```

> 本任务的失败测试**不提交**；等实现通过后与实现一起提交。

## 4. 语义与设计要点

1. **默认/空句柄**：`TaskHandle{}` 调用 `wait()` 立即返回，`ready() == false`。
2. **已完成句柄**：若 `ready() == true`，`wait()` 立即返回，不访问 `JobSystem`（即使 `JobSystem` 已经析构——注意 `~JobSystem()` 会先 `waitAll()`，因此 `JobSystem` 析构完成后所有已提交 handle 必然已 `completed == true`）。
3. **`workerCount == 0`**：`submit` 内联执行并在返回前已置 `completed = true`，`wait()` 立即返回。
4. **未完成句柄的等待循环**：
   - 只要 `!ready()`，优先尝试从所属 `JobSystem` 的队列 `try_pop` 出一个任务并走同一套 `finishTask_` 完成路径（执行任务 + `pendings_--` + 通知）；
   - 一旦当前 handle 变为 `ready()`，立即返回，**不等待**其他仍在运行的无关慢任务；
   - 若队列为空且 `!ready()`，进入条件变量等待，直到**"本任务完成"或"队列中有新任务可帮忙"**。
5. **需要你想清楚的同步问题（核心难点）**：
   - `waitAll()` 的结束条件是 `pendings_ == 0`，而 `TaskHandle::wait()` 的结束条件是 `state_->completed.load()`；
   - 当队列为空且本任务正在某个 worker 上执行时，调用线程需要睡眠等待两类事件之一：
     1. **本任务在 worker 上执行完毕**（此时可能还有其他慢任务在跑，即 `pendings_ > 0`！）；
     2. **等待期间有别的线程调用了 `submit()`**，队列里出现了新任务可以帮忙。
   - 请思考：`TaskHandle::wait()` 睡眠在哪一个 `condition_variable` / `mutex` 上？
     - 如果统一睡在 `JobSystem::cv_`（配 `JobSystem::mutex_`）：要注意当前 `finishTask_` 只在 `pendings_ == 0` 时才 `cv_.notify_all()`；若改成每个任务完成（或 `state->completed` 置位）也通知 `JobSystem::cv_`（或统一由 `JobSystem::mutex_` + `cv_` 管理等待），就能同时被"任意任务完成"和"`submit` 新任务"唤醒；
     - 如果睡在 `state_->cv`：`submit()` 新任务时并不知道有哪些 `state_->cv` 正在等待帮忙。
   - 锁顺序保持不变：`JobSystem::mutex_` → `MtQueue` 内部锁，绝不反向。

## 5. 测试（当前应为红）

`tests/job/test_handle_wait_help.cpp`（4 条）：

1. **wait executes queued target task on the main thread**：1 个 worker 被 `blocker` 占住，`target` 在队列里；主线程调 `target.wait()`，主线程自己从队列取出 `target` 执行并返回，此时 `blocker` 甚至还在跑。
2. **wait executes queued helper task to unblock its own task**：1 个 worker 正在跑 `blocker`（等待 `release`），`releaser` 在队列里；主线程调 `blocker.wait()`，主线程帮忙执行队列里的 `releaser` 从而释放 `blocker`。
3. **wait executes tasks submitted during the wait**：1 个 worker 正在跑 `blocker`，主线程先调 `blocker.wait()` 进入等待，另一线程 50ms 后才 `submit(releaser)`；验证等待中的 `handle.wait()` 能被 `submit` 唤醒并帮忙执行。
4. **wait returns when its own task finishes without waiting for slow tasks**：2 个 worker，一个跑 300ms 慢任务，一个跑 20ms 快任务；主线程调 `fast.wait()`，返回时 `fast.ready() == true` 且 `slow.ready() == false`（不会退化成 `waitAll()`，也不会因为 `pendings_ > 0` 漏通知而傻等慢任务）。

## 6. 验收

```bash
cmake --build --preset debug-vk
./build/debug-vk/bin/YrTests_job
ctest --test-dir build/debug-vk -R handle_wait_help
cmake --build --preset asan
ctest --preset asan -R handle_wait_help
```

要求：

- 4 条 `handle_wait_help` 用例通过；
- 现有 27 条 `mt_queue` / `job_system` / `task_handle` / `wait_help` 用例不回归；
- ASan 与 TSan（`setarch $(uname -m) -R ./build/tsan/bin/YrTests_job`）干净；
- `check_format.sh`、`check_deps.py` 通过。

## 7. Q&A / 决策记录

- 2026-11-16：本任务完成后，M10-a（线程池 + TaskHandle + 主线程帮忙）全部收口，下一步进入 M10-b（依赖图 `submit(fn, deps)` 与 `parallel_for`）。
