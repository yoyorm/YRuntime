# job 层（`yr_job`）

> 代码位置：`engine/job/include/yr/job/{mt_queue.h,job_system.h}` 与 `src/job_system.cpp`
> 线程池 + 任务完成句柄。依赖方向：**只依赖 `yr::core`**（`tools/check_deps.py` 强制）。
> 任务卡：`doc/tasks/m10-a-mtqueue.md`、`m10-a-job-system.md`、`m10-a-task-handle.md`、`m10-a-wait-help.md`

## 1. 线程模型

```
主线程（帧循环）                          worker × N
─────────────────                        ──────────────
submit(fn) ──► [ jobs: MtQueue<function> ] ──► wait_pop → finishTask_(fn)
   │                                                     │
   ├─ pendings_++（锁内）                                 ├─ fn()
   │                                                     └─ pendings_--（锁内）
   │                                                          归零 → cv_.notify_all()
   ▼
waitAll(): 循环 { 取任务就帮忙执行；取不到就判完成 / 在 cv_ 上等 }
```

- **并发只存在于 job 层**。scene / asset / render 仍是单线程；worker 只处理 POD 字节，`Object` / `Resource` 的构造与注册仍只在主线程（`AGENTS.md` §3）。
- **结果不回传**：`JobSystem` 只负责"执行完"，结果由调用方写进自己的共享结构，主线程在 `pump()` 阶段收（M9.2 的 `LoaderWorker` + `completed_` 模式）。

## 2. `MtQueue<T>`（`mt_queue.h`）

- **职责**：线程安全 FIFO；支持 move-only 元素；`close()` 后可继续 drain。
- **所有权**：队列拥有元素；调用方通过 `T& out` 取回。不可拷贝 / 移动。
- **API**：

  | 签名 | 语义 | 失败模式 |
  |---|---|---|
  | `void push(T value)` | 入队并唤醒一个等待者 | 已 `close` → 抛 `std::logic_error` |
  | `bool try_pop(T& out)` | 非阻塞；只关心"有没有元素" | 空 → `false`（**与 close 无关**） |
  | `bool wait_pop(T& out)` | 阻塞到有元素 **或**（已关闭且空） | 关闭且空 → `false` |
  | `void close()` | 幂等；停止接收；`notify_all` | — |
  | `bool closed() const noexcept` | 锁外查询（`atomic<bool>`，不加锁） | — |
  | `size_t size() const` | 加锁读，近似值 | — |

- **不变量**：
  - 谓词是 `is_closed || !queue_.empty()`；**先查后睡由 `cv.wait(lock, pred)` 承担**，醒来必重查。
  - `close()` 后 `push` 抛异常，已入队元素仍按 FIFO drain，drain 完 `wait_pop` 返回 `false`。
  - 析构前置条件：**没有线程仍阻塞在 `wait_pop`**（标准对 `condition_variable` 析构同样要求）。顺序固定为 `close()` → `join` → 析构。
  - `closed()` 声明 `noexcept`，因此**不能加锁**（`mutex::lock` 可能抛 `system_error` → `terminate`）；关闭标志用 `std::atomic<bool>`，写入仍在锁内。

## 3. `JobSystem`（`job_system.h` / `job_system.cpp`）

- **职责**：固定 worker 数的线程池；`submit` 返回 `TaskHandle`；`waitAll` 等待并**帮忙执行**排队任务。
- **所有权**：拥有 worker 线程与队列。不可拷贝 / 移动。`JobSystem` 必须比所有任务捕获的对象活得久。
- **API**：

  | 签名 | 语义 | 边界 |
  |---|---|---|
  | `JobSystem(size_t workerCount = hardware_concurrency())` | 启动 N 个 worker | `0` → 内联模式，不启动线程 |
  | `TaskHandle submit(function<void()> task)` | 入队（锁内 `push` + `pendings_++`），出锁后 `notify_all` | `workerCount==0` 时在调用线程执行完再返回 |
  | `TaskHandle submit(function<void()> task, span<const TaskHandle> deps)` | 前置依赖全部完成时才入就绪队列；`initializer_list` 重载支持 `{a, b}` | 同一 `JobSystem` 的句柄；DAG 由构造方式天然保证 |
  | `void waitAll()` | 帮忙执行排队任务直到 `pendings_ == 0` | 只允许**非 worker 线程**调用；可在多个非 worker 线程并发调用 |
  | `size_t workerCount() const noexcept` | 构造时配置值 | — |

- **执行路径（所有完成方共用）**：

  ```
  finishTask_(job):  job.task();  { lock; job.state->completed = true; --pendings_; }  cv_.notify_all();
  ```

  worker、`waitAll` 与 `TaskHandle::wait`（`waitFor`）的帮忙路径都调用它。每次任务完成都在同一临界区内同时更新 `state->completed` 与 `pendings_`，并在锁外 `notify_all`，从而同时唤醒等待单任务的 `waitFor` 与等待全局清空的 `waitAll`。

- **`waitAll` 的形状（不可简化掉的部分）**：

  ```
  for (;;) {
    取到任务 → task 执行 + finishTask_ → continue
    取不到   → 锁内判完成；未完成 → cv_.wait(pred = 完成 || 有活) → 回到循环顶部
  }
  ```

  - 谓词必须同时包含 **"全干完"** 与 **"有活可干（我要帮忙）"**；
  - `wait` 之后**无条件 `continue`**，不要在局部另写一套"完成"判断（`pendings_==0` 已等价于"队列空且无任务在跑"：`submit` 在同一把锁里 push+`++`，被取走的任务到跑完才 `--`）。
  - 不要忙等：不再 `continue` 空转；取不到就去 `cv_` 上睡。

- **锁顺序**：`JobSystem::mutex_` → `MtQueue` 内部锁（`submit` 持锁入队、`waitAll` 谓词持锁读 `size()`）。worker 侧是 `wait_pop` 返回后才拿 `mutex_`，两者不嵌套；**不得反向**。

- **析构顺序**：`~JobSystem()` 先 `waitAll()`（清空已提交任务）再 `queue_.close()`；随后成员逆序析构，`workers_` **必须最后声明**，以便 `std::jthread` 的隐式 join 早于 `queue_` / `mutex_` / `cv_` 的销毁。前提：析构期间不得有别的线程 `submit`，也不得在任务执行中销毁。

### 3.1 依赖图（`submit(fn, deps)`）

- **结构**：未就绪状态挂在 `TaskHandle::State` 上——`remaining`（未完成前置数）、`task`（保管中的任务体）、`dependents`（完成时唤醒的后继 `State` 列表）。不引入独立 `Pending` 类型。
- **就绪队列语义**：队列里**只有可以立刻执行的任务**。`remaining == 0` 的那一刻才 `queue_.push`；worker / `waitAll` / `TaskHandle::wait` 的帮忙路径都不判断依赖。
- **`submit` 顺序（持 `mutex_`）**：① `pendings_++`（必须在判就绪之前，否则 `waitAll` 会在"前置完成、后继未入队"的间隙提前返回）；② 跳过空/已完成 dep，其余 `dep.state_->dependents.push_back(state)` 并计数；③ `remaining == 0` 直接入队，否则保管任务体。
- **`finishTask_`（持 `mutex_`）**：置 `completed`、`--pendings_`、逐个 `--dependent->remaining`，归零者入就绪队列；最后 `clear()` `dependents` 释放引用，锁外 `notify_all`。
- **检查与挂载同一临界区**：`dep.completed` 检查与 `push_back` 必须同锁，否则会出现"检查未完成 → 完成方清空 → 再挂上"的 lost transition，后继永远不触发。
- **重复依赖** `{a, a}` 不去重：挂两次、`remaining = 2`，完成时同一循环减两次，第二次归零只入队一次。
- **环**：`TaskHandle` 只能在 `submit` 返回后存在，依赖只能指向更早提交的任务，故图天然是 DAG，不做环检测。

## 4. `TaskHandle`

- **职责**：单个任务的完成凭据；可拷贝，所有副本共享同一完成状态（`shared_ptr<State>`）。
- **API**：

  | 签名 | 语义 |
  |---|---|
  | `bool ready() const noexcept` | 任务完成后 `true`；默认 handle 恒 `false` |
  | `void wait() const` | 阻塞到完成；默认 handle 立即返回 |
  | `TaskHandle()` | 无效句柄（非拥有） |

- **`State` 字段与分工**：`State` 只含 `atomic<bool> completed`（服务锁外 `ready()`，写入在 `JobSystem::mutex_` 内）；`TaskHandle` 另持 `JobSystem* owner_`，未完成时委托 `owner_->waitFor(state_)` 在 `JobSystem::cv_` 上等待并帮忙执行排队任务（谓词 = `state->completed || queue_.size() > 0`）。
- **边界**：`JobSystem` 必须比所有未完成的 `TaskHandle` 活得久（`~JobSystem()` 会先 `waitAll()`）；不要在 worker 线程调 `wait()`；任务异常不在本阶段处理（任务不得抛出，否则 handle 永不 ready）。

## 5. 并发不变量（写/审 job 层代码时逐条对照）

1. **共享状态只在锁内改**。`atomic` 只替代"锁外的那一次读"，不能替代锁。
2. **检查 + 动作必须在同一临界区**。否则就是 check-then-act（丢任务）或 lost wakeup（永久睡）。
3. **谁改状态谁通知**：入队（`submit`）、归零（`finishTask_`）各算一次状态变更；通知在锁内或锁外都正确，锁外只是少一次争锁。
4. **等待者醒来必重查谓词**，且谓词要包含**所有**"该醒"的理由（完成 / 有活）。
5. **结束条件 ≠ 唤醒条件**：`waitAll` 的返回条件是 `pendings_ == 0`；唤醒条件是"完成 **或** 有活"。

## 6. 常见错误 → 症状

| 错误 | 症状 |
|---|---|
| `if (empty) wait()` 而不是谓词循环 | 醒来后读空容器 → 崩溃 / 读到垃圾 |
| 计数在锁外 `--`，或通知不带锁内状态变更 | 单次全绿，数百轮偶发挂死；TSan **不报**（数据不竞争，是逻辑竞态） |
| `waitAll` 只判"队列空" | 嵌套提交时提前返回 |
| `waitAll` 空转（取不到就立即重试） | 100% CPU，并与 worker 抢锁 |
| `waitAll` 只等不帮忙 | 1 个 worker 被占住时死锁 |
| `submit` 不 `notify` `cv_` | 等待期间新提交的任务永远得不到帮忙（worker 全忙时死锁） |
| 帮忙执行后不通知归零 | 其他 `waitAll` 等待者永远不醒 |
| 闭包按引用捕获任务/状态 | `stack-use-after-scope`（ASan），症状可为 HANG / SIGABRT / SIGSEGV |
| 析构时仍有线程阻塞在 `wait_pop` | UB（标准禁止）；`close` → `join` → 析构 |

## 7. 验证

```bash
cmake --build --preset debug-vk --target YrTests_job
./build/debug-vk/bin/YrTests_job "[job]"          # mt_queue / job_system / task_handle / wait_help / job_deps / parallel_for
ctest --test-dir build/debug-vk -R "job"          # 按用例名注册
cmake --build --preset asan && ./build/asan/bin/YrTests_job "[job]"

# TSan（正式 preset；本机/容器需要 tools/ctest_tsan.sh 关闭 ASLR）
cmake --preset tsan && cmake --build --preset tsan
./tools/ctest_tsan.sh -R "job|wait_help|mt_queue|task_handle|parallel_for"
```

| 手法 | 用途 | 命令要点 |
|---|---|---|
| 超时判挂 | 死锁 / 丢失唤醒 | `timeout 5 ./bin "用例名"`，`124` 即挂住 |
| 重复判 flake | 偶发竞态 | `for i in $(seq 1 200); do ... || echo red; done` |
| 断点看栈 | 不知道卡在哪 | `gdb -batch -ex run ...` + 向 gdb 发 `SIGINT`（本机 `ptrace_scope` 限制，不能 attach） |
| 时序放大 | 复现极低概率竞态 | 任务里注入 `sleep`、`taskset -c 0`、或 `LD_PRELOAD` 放慢 `pthread_cond_wait` |
| 变异测试 | 判断"测试有没有用" | 拷贝一份故意改坏一行，重编后看用例是否变红 |
| TSan | 数据竞争 | 正式 preset：`cmake --preset tsan` + `./tools/ctest_tsan.sh`（内部 `setarch -R` 关 ASLR） |

**已知测试弱点**：`wait_help` 的"返回时不能有任务在跑"那条是低频探针——变异测试（把 `wait` 后的 `continue` 换成"队列空就 return"）**无法稳定抓到**。该不变量真正的防线是 `wait` 之后无条件 `continue` 的结构，而不是这条用例。

## 8. 内存序决策

- 当前只有 `TaskHandle::State::completed` 是 `std::atomic<bool>`，使用默认 `seq_cst`：`ready()` 是锁外读，默认序最省心；写入在锁内，顺序已由 mutex 提供，改 `relaxed` 有收益但测不出来。
- `JobSystem::pendings_` 是**普通 `size_t`**，全部访问在 `mutex_` 内；`cv_` 的谓词也持同一把锁。
- 全程未使用 `acquire` / `release` / `acq_rel` 的手写配对；M10-c 若做 work stealing（当前不做），再逐处论证并写进头文件注释。

## 9. 遗留 / 已知缺口

- `JobStats`（M10 收口件）；`--jobs 0` 的单线程路径已有测试覆盖，运行时接线在 asset worker 迁移时再验证；
- 有界队列 / 背压 / 优先级 / 取消 / work stealing：明确不做；
- `AssetDatabase` 的 IO+解码尚未搬到 worker（M9.5d-0 与 M10 收口后合并执行）；
- TSan 已是正式 preset；`LoaderWorker` 成员初始化顺序竞态已在 2026-11-19 修复（TSan preset 首次全量运行发现）。