# M10-b-1：手写依赖图 `submit(fn, deps)`

> 状态：进行中（M10-a 已全部完成；AI 已准备契约与失败测试）
> 最近更新：2026-11-17

## 1. 目标

在 `JobSystem` 上新增带前置依赖的任务提交接口：

```cpp
#include <span>

class JobSystem final {
public:
  [[nodiscard]] TaskHandle submit(std::function<void()> task);

  // 当 deps 中所有有效任务完成后，task 才允许进入就绪队列执行。
  [[nodiscard]] TaskHandle submit(std::function<void()> task,
                                  std::span<const TaskHandle> deps);
};
```

为了方便调用方直接传初始化列表（如 `jobs.submit(fn, {a, b})`），可以只声明 `std::span<const TaskHandle> deps`（C++20 中 `std::initializer_list` 或 `std::vector` / `std::array` 均可隐式转换，或额外提供一个 `std::initializer_list<TaskHandle>` 转发重载）。

## 2. 为什么先做它（以及为什么你 M10-a-5 的设计正好铺好了路）

在 M10-a-5 中，你选择了**方案 B**：
- `queue_` 里存放的是 `Job{task, state}`（即 **`queue_` 就是就绪队列 Ready Queue**）；
- `finishTask_` 在 `JobSystem::mutex_` 临界区内把 `job.state->completed = true` 并 `--pendings_`。

这让依赖图的实现变得非常自然——**任务未就绪时不进 `queue_`，只有当所有前置依赖都完成（依赖计数归零）时，才推入 `queue_`**。

## 3. 核心语义与不变量

1. **前置依赖全部完成才执行**：
   - 设 `deps` 中包含 $k$ 个有效且尚未完成的 `TaskHandle`；
   - 只要还有任一前置依赖未完成，`task` **不能**被 worker 或主线程帮忙执行；
   - 当最后一个未完成的前置依赖在 `finishTask_` 中完成时，该任务变为就绪并推入 `queue_`。
2. **边缘输入处理**：
   - `deps` 为空（`deps.empty()`）：等价于普通无依赖 `submit(std::move(task))`；
   - `deps` 中包含默认构造的无效 `TaskHandle{}` 或已完成的 `TaskHandle`：直接视为已满足（不计入未完成依赖数）；
   - `deps` 中包含**重复的同一个未完成 `TaskHandle`**（例如 `{h, h}`）：无论按“去重”还是按“每出现一次触发一次递减”实现，当 `h` 完成时该后继任务都必须正常就绪并执行一次（不能卡死，也不能执行两次）；
   - **`workerCount == 0`（内联模式）**：所有前置 `deps` 在它们各自 `submit` 返回前就已经内联执行完毕（`ready() == true`），因此 `submit(task, deps)` 同样直接内联执行 `task()` 并返回已完成的 `TaskHandle`。
3. **`pendings_` 与 `waitAll()` / `TaskHandle::wait()` 的协作**：
   - **关键不变量**：`submit(task, deps)` 调用时，即便 `task` 因为依赖未满足还不能进 `queue_`，它也已经是一个“已提交但未完成”的任务，因此**必须在 `submit` 时就计入 `pendings_++`**！
   - 否则：若前置任务 A 刚跑完把 `pendings_` 减到 0，而依赖 A 的后继任务 B 还没加进 `pendings_`，正在 `waitAll()` 的主线程就会误判“全部任务已完成”而提前返回。
   - 当 `submit`、前置完成触发入队、`finishTask_` 都在同一把 `JobSystem::mutex_` 下维护 `state->completed`、后继列表与 `pendings_` 时：
     - A 完成 → 锁内置 `A.completed = true`、把未决依赖归零的 B 推入 `queue_`、`--pendings_`（此时因 B 在 `submit` 时已 `++pendings_`，`pendings_` 仍 $\ge 1$）→ 锁外 `cv_.notify_all()`；
     - 正在 `waitAll()` 或 `B.wait()` 的主线程被唤醒后，看到 `queue_.size() > 0`，立刻可以帮忙执行 B！
4. **生命周期与环假设**：
   - 调用方只能传入由**同一个 `JobSystem`** 创建的 `TaskHandle`（或默认空句柄）；
   - 由于 `TaskHandle` 只在 `submit` 返回后才存在，正常调用天然构成有向无环图（DAG），不可能出现依赖环。

## 4. 推荐的数据结构思路（供参考，具体由你决定）

在 `JobSystem::mutex_` 已经保护 `state->completed` 的前提下，最简结构只需要在 `State`（或 `Job`）上增加两样东西：

- **前置侧（`TaskHandle::State`）**：当自己完成时，需要知道“有哪些后继在等我”——例如 `std::vector<std::shared_ptr<...>> dependents;`（或后继 `State`/`DepNode` 列表）；
- **后继侧**：保存自己的 `Job{task, state}` 以及“还剩几个前置没完成”的计数 `std::size_t remainingDeps = 0;`。
- **`submit(task, deps)` 临界区（持 `JobSystem::mutex_`）**：
  1. `pendings_++`；
  2. 遍历 `deps`：若 `dep.state_` 非空且 `!dep.state_->completed.load()`，则 `remainingDeps++` 并把后继节点挂到 `dep.state_->dependents`；
  3. 若遍历完 `remainingDeps == 0`，直接 `queue_.push(Job{...})`；
- **`finishTask_(Job)` 临界区（持 `JobSystem::mutex_`）**：
  1. `job.state->completed = true;`
  2. `--pendings_;`
  3. 遍历 `job.state->dependents`：对每个后继 `--remainingDeps`，若归零则 `queue_.push(...)`；清空 `dependents` 释放引用；
  4. 出锁后 `cv_.notify_all()`。

> 思考点：为什么步驟 2 检查 `!dep.state_->completed` 和挂载 `dependents` 必须在 `JobSystem::mutex_` 锁内？
> 因为如果放在锁外，可能在检查完“未完成”之后、挂进 `dependents` 之前，worker 刚好跑完 `finishTask_` 清空了 `dependents`，导致后继任务永远无人触发（经典的 lost transition）。而在同一把 `JobSystem::mutex_` 内做，就天然原子化了。

## 5. 文件所有权

**用户手写（AI 不得改实现）：**

```text
engine/job/include/yr/job/job_system.h
engine/job/src/job_system.cpp
```

**AI 负责：**

```text
tests/job/test_job_deps.cpp
tests/job/CMakeLists.txt
doc/tasks/**
```

> 红灯规则：AI 准备的 `submit(fn, deps)` 占位声明与 `tests/job/test_job_deps.cpp` **不提交**，留在工作区等你实现通过后一起提交。

## 6. 测试（当前应为红）

`tests/job/test_job_deps.cpp`（6 条）：

1. **single dependency ordering (A → B)**：A 睡眠 20ms 后写标志，B 依赖 A 并读取该标志；验证 B 一定在 A 完成后才开始执行。
2. **diamond dependency (A → B, C → D)**：A 产出数据，B 和 C 并发依赖 A 各自写结果，D 同时依赖 `{B, C}` 汇总结果；验证 `D.wait()` 返回时 A/B/C/D 顺序与结果全部正确。
3. **already-completed and default handles in deps**：`deps` 中混入已 `wait()` 完成的句柄与默认空 `TaskHandle{}`；验证任务立即就绪执行，不卡死。
4. **duplicate handle in deps (`{A, A}`)**：同一个未完成句柄在 `deps` 中出现两次；验证 A 完成后 B 正常执行且只执行一次。
5. **`waitAll` covers tasks still waiting on dependencies**：提交长链 `A → B → C → D`，主线程直接调 `jobs.waitAll()`；验证不会在 A 完成、B 刚解锁的间隙提前返回。
6. **`workerCount == 0` inline execution with deps**：0 线程模式下提交 `A → B → C`，验证按序内联完成且句柄均为 `ready()`。

## 7. 验收

```bash
cmake --build --preset debug-vk --target YrTests_job
./build/debug-vk/bin/YrTests_job
ctest --test-dir build/debug-vk -R job_deps
cmake --build --preset asan --target YrTests_job
./build/asan/bin/YrTests_job
```
