# M10-a-4：waitAll 主线程帮忙执行

> 状态：进行中（AI 已准备失败测试；实现由用户完成）
> 最近更新：2026-11-15

## 1. 目标

让 `JobSystem::waitAll()` 在等待期间**主动执行队列中的任务**，而不是只阻塞等待 worker：

```cpp
void JobSystem::waitAll() {
  for (;;) {
    std::function<void()> task;
    if (queue_.try_pop(task)) {
      // 主线程帮忙执行
      runTask(task);
      continue;
    }

    std::unique_lock lock{mutex_};
    if (!pendings_) {
      return;
    }
    cv_.wait(lock);
  }
}
```

要点：

- worker 和主线程执行同一个 `runTask` 路径（执行任务 + pending-- + 归零 notify）；
- `submit` 在入队后唤醒 `cv_`，让正在 `waitAll` 的主线程能发现新任务并帮忙；
- worker 侧仍在 pending 归零时 notify；
- `workerCount == 0` 路径不变（submit 内联，waitAll 立即返回）。

## 2. 为什么

当前 `waitAll()` 只等待：如果一个 worker 被一个“等待后续任务”的任务占住，而后续任务还在队列里，主线程帮不上忙，可能长时间空转甚至死锁。

M10 的验收要求主线程在 wait 时帮忙执行，这是后续依赖图 / parallel_for 的基础。

## 3. 文件所有权

**用户手写（AI 不得改实现）：**

```text
engine/job/include/yr/job/job_system.h
engine/job/src/job_system.cpp
```

**AI 负责：**

```text
tests/job/test_wait_help.cpp
tests/job/CMakeLists.txt
doc/tasks/**
```

> 本任务的失败测试**不提交**；等实现通过后与实现一起提交。

## 4. 语义 / 边界

- `waitAll` 可以执行任意排队任务；如果任务本身阻塞等待其它排队任务，主线程可能被占住——这是调用方的任务设计问题，文档写清楚；
- `waitAll` 返回时 `pendings_ == 0`；
- 帮忙执行的任务同样必须递减 pending，并在归零时 `notify_all`；
- 不能重复执行同一任务；
- `submit` 需要唤醒 `cv_`，否则主线程在等待中看不到新任务；
- `workerCount == 0` 时没有队列/worker，行为保持不变；
- `TaskHandle::wait()` 本阶段仍然只阻塞；主线程帮忙等待 handle 留到后续。

## 5. 测试（当前应为红）

`tests/job/test_wait_help.cpp`：

1. 1 个 worker + 1 个 blocker 占住 worker；
2. 再提交一个 releaser；
3. 主线程 `waitAll()`；
4. 如果主线程帮忙，releaser 在 main 上执行并释放 blocker；断言 `helper_ran_on_main == true`；
5. 为了在“没有帮忙”的实现下不挂死，watchdog 500ms 后释放 blocker；此时测试会失败，但不会卡住进程。

## 6. 验收

```bash
cmake --build --preset debug-vk
./build/debug-vk/bin/YrTests_job
ctest --test-dir build/debug-vk -R wait_help
cmake --build --preset asan
ctest --preset asan -R wait_help
```

要求：

- `wait_help` 用例通过；
- 现有 MtQueue / JobSystem / TaskHandle 用例不回归；
- ASan 干净；
- 连续运行不出现丢任务/重复执行；
- `check_format.sh` 通过。

## 7. 明确不做

- `TaskHandle::wait()` 的主线程帮忙（后续）；
- 依赖图 `submit(fn, deps)` / `parallel_for`（M10-b）；
- 取消/优先级/有界队列；
- work stealing（M10-c）。

## 8. Q&A / 决策记录

- 2026-11-15：`waitAll` 帮忙执行的顺序是“先 try_pop 帮忙，再在 cv 上等待”；submit 必须 notify cv，否则主线程看不到新任务。
- 2026-11-15：主线程执行的任务和 worker 走同一 pending 递减/notify 路径，避免两套完成逻辑。
- 2026-11-15：测试用 watchdog 防止“未实现帮忙”时死锁；`helper_ran_on_main` 才是断言重点。
