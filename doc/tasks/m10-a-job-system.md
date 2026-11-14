# M10-a-2：手写 JobSystem

> 状态：进行中（MtQueue 已完成；AI 已准备失败测试与骨架）
> 最近更新：2026-11-14

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
