# M10-a-3：TaskHandle

> 状态：**已完成**（2026-11-15：6/6 TaskHandle 用例通过；全量 527/527；ASan/TSan 干净）
> 最近更新：2026-11-15

## 1. 目标

让 `JobSystem::submit` 返回一个可拷贝的完成句柄：

```cpp
class TaskHandle {
public:
  TaskHandle() = default;

  [[nodiscard]] bool ready() const noexcept;
  void wait() const;

private:
  struct State;
  std::shared_ptr<State> state_;
  friend class JobSystem;
};
```

- `ready()`：任务完成后 true；
- `wait()`：阻塞到任务完成；默认/无效 handle 直接返回；
- 可拷贝；所有副本共享同一个完成状态；
- `submit` 返回的 handle 在任务完成后 `ready()==true`；
- `workerCount == 0` 时 `submit` 内联执行，返回的 handle 在 `submit` 返回前已经 ready。

## 2. 文件所有权

**用户手写（AI 不得改实现）：**

```text
engine/job/include/yr/job/job_system.h
engine/job/src/job_system.cpp
```

AI 已做契约 scaffold：

- 增加 `TaskHandle` 声明；
- `submit` 返回 `TaskHandle`；
- `job_system.cpp` 中 `TaskHandle::State` / `ready` / `wait` / `submit` 目前是 stub；
- 现有 JobSystem 测试已改为 `(void)jobs.submit(...)`。

**AI 负责：**

```text
tests/job/test_task_handle.cpp
tests/job/CMakeLists.txt
doc/tasks/**
```

## 3. 语义

- 每个 `submit` 创建独立完成状态；
- 任务完成（正常返回）后置 ready 并唤醒等待者；
- `wait()` 允许多个线程/多个 handle 副本同时等待；
- 默认构造的 `TaskHandle`：`ready()==false`，`wait()` 立即返回；
- 任务异常仍不在本阶段处理（任务不得抛出）；
- `waitAll()` 行为保持不变；`TaskHandle` 不改变 pending 计数语义；
- 不要在 worker 线程调用 `TaskHandle::wait()`（会占用 worker；本阶段不保证）。

## 4. 测试（当前应为红）

`tests/job/test_task_handle.cpp`：

1. `submit` 返回 handle，`waitAll()` 后 `ready()` 为 true；
2. 长任务：`wait()` 前 `ready()` false，`wait()` 后任务完成且 ready；
3. handle 拷贝：两个副本共享状态，都能 wait；
4. `JobSystem(0)` 内联：`submit` 返回时任务已执行、handle ready；
5. 默认 handle：`ready()` false，`wait()` 不阻塞；
6. 已 ready 的 handle 再次 `wait()` 立即返回。

## 5. 验收

```bash
cmake --build --preset debug-vk
./build/debug-vk/bin/YrTests_job
ctest --test-dir build/debug-vk -R task_handle
cmake --build --preset asan
ctest --preset asan -R task_handle
```

要求：

- 全部 `task_handle` 用例通过；
- 现有 17 个 JobSystem/MtQueue 用例不回归；
- ASan 干净；
- 并发 wait 可重复运行；
- `check_format.sh` 通过。

## 6. 明确不做

- 主线程 `waitAll` 帮忙执行任务（M10-a-4）；
- 依赖图 / `parallel_for`（M10-b）；
- 任务返回值 / 异常上报；
- move-only 任务；
- 取消单个任务；
- work stealing（M10-c）。

## 7. Q&A / 决策记录

> 设计问答的结论回填到这里；不要只留在聊天里。

- 2026-11-15：`TaskHandle` 可拷贝，内部用 `shared_ptr<State>` 共享完成状态；默认 handle 是非拥有空句柄。
- 2026-11-15：`ready()` 是 `const noexcept`；实现建议用 atomic/共享状态，不要在 `ready()` 里加可能抛异常的锁。
- 2026-11-15：`workerCount == 0` 的 submit 必须在返回前把 handle 置 ready，否则调用方会误判。
- 2026-11-15：`wait` 只保证主线程/非 worker 等待；主线程帮忙执行留到下一小步。
- 2026-11-15：TaskHandle 的 stub 与失败测试**不提交**；等实现通过后与 `job_system.h/.cpp` 一起提交，避免 CI 红灯。
- 2026-11-15：实现采用每任务 `State{atomic<bool> completed, mutex, cv}`。`ready()` 只读 atomic；`wait()` 在 state 锁下等 predicate；完成方同一锁内置位，锁外 `notify_all`，无 lost wakeup。
- 2026-11-15：`submit` 把任务包成 `lambda`，执行完置 completed；`workerCount == 0` 路径在返回前直接置位，保证 handle 已 ready。
- 2026-11-15：验证：`YrTests_job` 23/23、81 assertions；debug 100 轮 0 失败；ASan 527/527；TSan（setarch -R）20 轮 0 失败。
