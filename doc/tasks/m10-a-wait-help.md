# M10-a-4：waitAll 主线程帮忙执行

> 状态：**已完成**（2026-11-16：`wait_help` 4 用例通过；`[job]` 200 轮 0 失败；全量 531/531；ASan 干净；TSan 10 轮 0 race+0 挂）
> 最近更新：2026-11-16

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

## 5. 测试

`tests/job/test_wait_help.cpp`（4 条）：

1. **queued tasks on the main thread**：1 个 worker + blocker 占住；releaser 在 `waitAll()` 之前入队；断言 `helper_ran_on_main == true`。watchdog 500ms 兜底，未实现时失败而不挂死。
2. **tasks submitted during the wait**：releaser 在 `waitAll()` **之后**由另一线程提交；验证 `submit` 的 `notify` 链路。watchdog 失败路径会补一个空任务，让失败表现为断言而不是超时。
3. **every waiter wakes when a helper finishes the last task**：双等待者 + 主线程跑最后一个任务；验证"谁减到 0 谁 `notify_all`"。
4. **does not return while a task is still running**：12 轮重复"等待线程被新任务唤醒、随后任务被 worker 抢走"的时序；断言返回时目标任务已完成。

> 变异测试结论（2026-11-16，只在 `/tmp` 副本上做）：删掉 `submit` 的 notify → 用例 2 变红；删掉 `finishTask_` 的归零 notify → 用例 3 变红（断言先打印，进程随后挂住，ctest 以 TIMEOUT 收口）；把 `wait` 之后的 `continue` 换成"队列空就 return" → **用例 4 未能稳定抓到**（`pred` 求值到 `size()` 复查之间的窗口只有几条指令）。用例 4 保留为低频回归/压力探针，不作为该 bug 的可靠防线；真正的防线是"wait 之后无条件 `continue`"这一结构。

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
- 2026-11-16：实现收敛为私有 `finishTask_(task)`：执行任务 + 锁内 `--pendings_` + 归零 `notify_all`；worker 与 `waitAll` 的帮忙路径共用。这样"最后一个完成者负责通知"是结构保证，不再依赖各处记得补 notify。
- 2026-11-16：`waitAll` 只保留一个 `finish` 判断（锁内算 `pendings_ == 0` 即可；`&& !queue_.size()` 在不变量下冗余——`submit` 在同一把锁里 push+`++`，被取走的任务直到跑完才 `--`，所以持锁观察到 `pendings_ == 0` 就等价于"队列空且无任务在跑"）。`cv_.wait` 返回后无条件 `continue`，把判断交回循环顶部，避免出现第二套"完成"判据。
- 2026-11-16：`submit` 的 `notify_all` 移到锁外（锁内正确但会让被唤醒者立刻撞锁）。
- 2026-11-16：补测试时用变异测试验证有效性（见 §5 注）。用例 3、4 的兜底 rescue 依赖 `submit` 的 notify，因此在"缺 notify"的变异下失败可能表现为"断言先失败、随后进程挂住"，ctest 侧以 TIMEOUT 收口；这是有意保留的取舍，不再为测试添加私有唤醒接口。
