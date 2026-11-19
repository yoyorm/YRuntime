# M10-b-2：手写 `parallel_for(n, batch, fn)`

> 状态：**已完成**（2026-11-19：`parallel_for` 6/6 通过；`[job]` 44 cases / 2771 assertions；300 轮 0 失败；全量 548/548；ASan/TSan 干净）
> 最近更新：2026-11-19

## 1. 目标

在 `JobSystem` 上提供一个"把区间切成批次并行执行、阻塞到全部完成"的入口：

```cpp
class JobSystem final {
public:
  // 把 [0, count) 按 batchSize 切成不重叠区间并发执行；
  // 阻塞到本调用提交的所有批次完成。
  void parallelFor(std::size_t count, std::size_t batchSize,
                   std::function<void(std::size_t begin, std::size_t end)> fn);
};
```

## 2. 语义（必须实现的部分）

1. **切分**：批次为 `[b * batchSize, min((b + 1) * batchSize, count))`，批次数 `ceil(count / batchSize)`；
2. **覆盖**：每个下标恰好被访问一次，批次之间不重叠；
3. **边界**：
   - `count == 0`：不提交任何任务，立即返回；
   - `batchSize == 0`：视作 `1`（每个元素一个批次）；
   - `batchSize > count`：只有 1 个批次 `(0, count)`；
4. **等待范围**：只等待**本调用提交的批次**。若同时有无关的慢任务在跑，`parallelFor` 返回时**不要求**它已完成（与 `TaskHandle::wait()` 的哲学一致，不是 `waitAll()`）；
5. **`workerCount == 0`**：内联顺序执行所有批次（调用线程），行为等价于普通循环；
6. **可帮忙**：等待期间主线程应帮忙执行排队任务（复用 `TaskHandle::wait()` 即可，不需要新等待逻辑）；
7. **只允许非 worker 线程调用**（本阶段由调用方保证，不做断言）。

## 3. 明确不做

- 跨批次归约 / 确定性求和接口——`fn` 若写共享目标，调用方必须自行保证**无数据竞争**且**与串行结果一致**（浮点求和顺序问题见 `00-vision.md` 与 roadmap M10）；
- 负载均衡 / 动态拆分 / work stealing（M10-c 已移入 Stretch）；
- 嵌套 `parallel_for`（worker 内调用）的保证；
- 返回每批次的 `TaskHandle`。

## 4. 推荐实现形状（供参考）

```cpp
void JobSystem::parallelFor(size_t count, size_t batchSize, function<void(size_t, size_t)> fn) {
  if (count == 0) {
    return;
  }
  if (batchSize == 0) {
    batchSize = 1;
  }
  const size_t batches = (count + batchSize - 1) / batchSize;

  // 内联模式：直接顺序执行
  // 否则：每批 submit 一个 lambda，收集 TaskHandle，逐个 wait()
}
```

要点：`fn` 会被多个批次共享，**按值拷贝**进每个批次 lambda（`std::function` 可拷贝），不要 `std::move` 给第一个批次后其它批次拿到空函数。

## 5. 文件所有权

**用户手写（AI 不得改实现）：**

```text
engine/job/include/yr/job/job_system.h
engine/job/src/job_system.cpp
```

**AI 负责：**

```text
tests/job/test_parallel_for.cpp
tests/job/CMakeLists.txt
doc/tasks/**
```

> 红灯规则：占位声明与失败测试**不提交**，实现通过后与实现一起提交。

## 6. 测试

`tests/job/test_parallel_for.cpp`（6 条，已全绿）：

1. **covers every index exactly once**：`count=1000`、`batch=64`、4 worker；每个下标原子计数恰好 1，总和等于 0..999 之和；
2. **batch larger than count runs a single batch**：`count=10`、`batch=64`；`fn` 恰好被调用一次且参数为 `(0, 10)`；
3. **count zero does nothing**：`count=0` 时 `fn` 不被调用、立即返回；
4. **zero batch size means one element per batch**：`batch=0`、`count=64`；每个下标恰好 1 次；
5. **zero workers execute inline in order**：`JobSystem(0)`；记录访问顺序，断言严格 `0..count-1`；
6. **returns without waiting for unrelated slow task**：1 个 worker 被 500ms 慢任务占住，`parallelFor` 完成自己的批次后立即返回（此时慢任务未完成），不会退化成 `waitAll()`。

## 7. 验收

```bash
cmake --build --preset debug-vk --target YrTests_job
./build/debug-vk/bin/YrTests_job
ctest --test-dir build/debug-vk -R parallel_for
cmake --build --preset asan --target YrTests_job
./build/asan/bin/YrTests_job
```

要求：

- 6 条 `parallel_for` 用例通过；
- 现有 38 条 `[job]` 用例不回归；
- ASan / TSan 干净，连续多轮运行不出现"漏执行 / 重复执行"。

## 8. Q&A / 决策记录

- 2026-11-19：签名用 `std::function<void(size_t begin, size_t end)>` 按值传参；`fn` 需要拷贝进每个批次，避免 move 后其它批次拿到空函数。
- 2026-11-19：等待语义选择"只等自己的批次"而非 `waitAll()`——与 `TaskHandle::wait()` 一致，避免因为无关慢任务把 `parallel_for` 变成全系统屏障。
- 2026-11-19：**批次数的溢出安全写法**：`count / batchSize + (count % batchSize == 0 ? 0 : 1)`，不用 `(count + batchSize - 1) / batchSize`（后者在 `count + batchSize - 1` 溢出时错误）。
- 2026-11-19：**区间终点**用 `count - begin < batchSize ? count : begin + batchSize`，避免 `begin + batchSize` 溢出。
- 2026-11-19：实现直接复用 `submit(each batch)` + 逐个 `handle.wait()`：帮忙执行由 `TaskHandle::wait()` 提供，`parallel_for` 本身不引入新的等待原语，也不需要改 `waitAll`。
- 2026-11-19：`workerCount == 0` 无特判：每批 `submit` 内联执行并返回 ready handle，随后 `wait()` 立即返回，天然得到"顺序内联"语义（测试 5）。
- 2026-11-19：`handles.reserve(batches)`；`count == 0` 提前返回，`batchSize == 0` 归一为 1。- 2026-11-19：批次数用 `count / batchSize + (count % batchSize != 0)` 计算，避免 `(count + batchSize - 1)` 的溢出；每批 `end` 用 `(count - begin < batchSize) ? count : begin + batchSize`，同样避免 `begin + batchSize` 溢出。
- 2026-11-19：实现复用通用路径两步：`submit` 每批 → 逐个 `handle.wait()`。**内联模式不需要特判**：`workerCount == 0` 时 `submit` 在调用线程立即执行并返回已完成句柄，循环顺序提交即顺序执行，正好满足"访问顺序 0..count-1"。
- 2026-11-19：等待的是 `vector<TaskHandle>` 里的每批句柄，不是 `waitAll()`——后者会把无关慢任务也等掉（测试 6）。主线程帮忙执行由 `TaskHandle::wait()`（M10-a-5）免费提供，`parallelFor` 不再写任何等待逻辑。
- 2026-11-19：`fn` 按值拷贝进每个批次 lambda；move 会让后续批次拿到空 `std::function`（变异测试：SIGABRT / `bad_function_call`）。
- 2026-11-19：内存实测（`operator new` 计数探针）：普通 `submit` 约 **136 B/任务、1.1 次分配**（`make_shared<State>` 88 B + deque 摊 51 B）；`parallelFor` 每批约 **208 B、2.1 次分配**（State 88 + 批次 lambda 堆缓冲 ~64 + deque 51），再加 `vector<TaskHandle>` 24 B/批。判断标准：任务工作量 ≥10 µs（或每批 ≥ 数千元素）时开销可忽略；风险主要来自批次数（`count=1e6, batch=1` ≈ 232 MB + 100 万次 submit），不是单任务字节。
- 2026-11-19：变异测试（`/tmp` 副本）：改用 `waitAll()` → 红（测试 6）；`fn` 改 move → SIGABRT；`end` 去掉 min → SIGSEGV；不归一化 `batchSize == 0` → SIGFPE。四种都被 6 条用例捕获。
