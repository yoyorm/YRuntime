# M10-a-2：手写 JobSystem

> 状态：**已完成**（2026-11-15：7/7 用例通过；debug 300 轮无失败；ASan 与 TSan 干净）
> 最近更新：2026-11-15

## 1. 目标

在 `MtQueue` 之上实现一个固定 worker 数的任务系统：

```cpp
namespace yr::job {

class JobSystem {
public:
  explicit JobSystem(std::size_t workerCount = std::thread::hardware_concurrency());
  ~JobSystem();

  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;
  JobSystem(JobSystem&&) = delete;
  JobSystem& operator=(JobSystem&&) = delete;

  // 提交任务；workerCount == 0 时在调用线程同步执行。
  void submit(std::function<void()> task);

  // 等待当前已提交（含任务内继续提交）的任务全部完成。
  // 只允许从非 worker（通常主线程）调用。
  void waitAll();

  [[nodiscard]] std::size_t workerCount() const noexcept;
};

} // namespace yr::job
```

## 2. 语义

- `workerCount > 0`：构造时启动 N 个 worker；worker 循环从 `MtQueue` 取任务执行。
- `workerCount == 0`：不启动线程；`submit` 在调用线程内联执行；`waitAll` 立即返回。
- `submit` 可从 worker 内部再次调用（嵌套提交）。
- `waitAll`：
  - 等待 pending 计数归零；
  - 必须覆盖嵌套提交产生的任务；
  - 不允许在 worker 线程调用（会死锁；本阶段由调用方保证）。
- 析构：
  - 停止接收新任务；
  - 唤醒所有 worker；
  - 等待已入队任务完成；
  - join 所有 worker；
  - 不得在任务执行中销毁。
- 任务异常：本阶段要求任务不得抛出；不要求捕获/上报。后续可加 `TaskResult`。
- `workerCount()` 返回构造时配置的值。
- 任务类型为 `std::function<void()>`，因此任务必须可拷贝；move-only 捕获不在本阶段范围。
- `MtQueue` 已实现，直接复用；不要重新写队列。

## 3. 文件所有权

**用户手写（AI 不得改实现）：**

```text
engine/job/include/yr/job/job_system.h
engine/job/src/job_system.cpp
```

**AI 负责：**

```text
tests/job/test_job_system.cpp
engine/job/CMakeLists.txt
tests/job/CMakeLists.txt
AGENTS.md / doc/tasks/**
```

## 4. 测试（当前应为红）

`tests/job/test_job_system.cpp`：

1. `JobSystem(0)` 内联执行，`workerCount()==0`，`waitAll()` 安全；
2. `JobSystem(4)` 执行 200 个任务，`waitAll()` 后计数完整；
3. 任务内嵌套提交，`waitAll()` 覆盖两层任务；
4. 析构时 drain 已提交任务（scope 退出后计数完整）；
5. 空系统 `waitAll()` 立即返回；
6. 多次 `waitAll()` 幂等；
7. `workerCount()` 返回配置值。

建议你先写最小实现，再跑测试逐步补齐。

## 5. 验收

```bash
cmake --build --preset debug-vk
./build/debug-vk/bin/YrTests_job
ctest --test-dir build/debug-vk -R job_system
cmake --build --preset asan
ctest --preset asan -R job_system
```

要求：

- 全部 `job_system` 用例通过；
- ASan 干净；
- 线程测试可重复运行，不出现死锁/丢任务；
- TSan 可选：`setarch $(uname -m) -R ./build/tsan/bin/YrTests_job`；
- `check_format.sh` 通过。

## 6. 明确不做

- `TaskHandle` / 单任务等待（下一小步）；
- 主线程 `waitAll` 时帮忙执行任务（下一小步）；
- 依赖图 `submit(fn, deps)`、`parallel_for`（M10-b）；
- work stealing（M10-c）；
- 有界队列/背压/优先级/取消；
- 任务异常上报。

## 7. Q&A / 决策记录

> 设计问答的结论回填到这里；不要只留在聊天里。

- 2026-11-14：`workerCount == 0` 定义为“调用线程内联执行”，用于保留单线程路径（M10 验收要求 `--jobs 0` 仍可工作）。
- 2026-11-14：`waitAll` 只允许从非 worker 调用；主线程帮忙执行留到下一小步，避免第一次实现就引入复杂唤醒。
- 2026-11-14：任务类型先用 `std::function<void()>`；move-only 任务/结果等到 TaskHandle 阶段再评估。
- 2026-11-15：**`pendings_` 的减一与"是否归零"判断必须在 `waitAll` 用的同一把锁内**，`notify_all` 可以放在锁外。曾把 `fetch_sub` 放在锁外：7 条用例单次全绿，但 200 轮里挂 3 次（约 1.5%），TSan 下 7 条里挂 2 条——这是 lost wakeup（逻辑竞态），不是数据竞争，ASan/TSan 都不会报 race。
- 2026-11-15：通知只在归零时发一次，而不是每个任务完成都 `notify_all`；`submit` 不通知这个 cv（worker 的唤醒由 `MtQueue::push` 负责），避免把 `waitAll` 的等待者提前叫醒。
- 2026-11-15：验收标准定为"**单次全绿不算数**"：`[job_system]` 连跑 300 轮无失败 + TSan 连跑 20 轮无挂且无 race 报告。并发 bug 的概率会随任务数放大（2 / 16 / 200 三种规模就是为制造时序差异）。
- 2026-11-15：锁顺序固定为 `JobSystem::mutex_` → `MtQueue` 内部锁（`submit` 持锁入队）。worker 侧是 `wait_pop` 返回后才拿 `mutex_`，两者不嵌套；不得反向。
- 2026-11-15：TSan 需要 `setarch $(uname -m) -R` 绕开 ASLR 与 TSan 的映射冲突；且**每次都要用当前源码重编**——跑旧二进制会得到与代码无关的 hang。
