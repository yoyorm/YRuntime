# M10-a：手写 MtQueue

> 状态：进行中（AI 已准备失败测试与 CMake 骨架，等待用户实现）
> 最近更新：2026-11-14

## 1. 目标

实现一个线程安全、支持 move-only 元素、可通过 `close()` 唤醒等待者的 FIFO 队列：

```cpp
namespace yr::job {

template <typename T>
class MtQueue {
public:
  MtQueue() = default;
  ~MtQueue();

  MtQueue(const MtQueue&) = delete;
  MtQueue& operator=(const MtQueue&) = delete;

  // 入队；若队列已 close，抛 std::logic_error。
  void push(T value);

  // 非阻塞；空队列返回 false。
  [[nodiscard]] bool try_pop(T& out);

  // 阻塞直到有元素或队列已 close 且为空。
  // 返回 true 表示 out 有效；false 表示已 close 且没有剩余元素。
  [[nodiscard]] bool wait_pop(T& out);

  // 幂等；停止接收新元素，唤醒所有 wait_pop。
  // 已入队的元素仍可被 pop；队列清空后 wait_pop 返回 false。
  void close();

  [[nodiscard]] bool closed() const noexcept;

  // 当前队列元素数量（近似值即可，但调用期间必须线程安全）。
  [[nodiscard]] std::size_t size() const;
};

} // namespace yr::job
```

## 2. 语义

- 严格 FIFO；
- 所有公开方法可被多个线程并发调用；
- `push` 接受 move-only `T`，不得要求 `T` 可拷贝；
- `close()`：
  - 之后的 `push` 抛 `std::logic_error`；
  - 唤醒所有阻塞的 `wait_pop`；
  - 已入队元素仍按 FIFO 弹出；
  - 队列为空后 `wait_pop` 返回 `false`；
  - 幂等；
- `wait_pop` 不允许 busy wait；
- 析构时不得有线程仍在 `wait_pop`；测试会先 `close()` 再 join；
- `closed()` 在 `close()` 后返回 true；
- `size()` 是近似值，但必须无数据竞争。

## 3. 文件所有权

**用户手写（AI 不得改实现）：**

```text
engine/job/include/yr/job/mt_queue.h
```

**AI 负责：**

```text
tests/job/test_mt_queue.cpp
tests/job/CMakeLists.txt
engine/job/CMakeLists.txt
CMakeLists.txt / tests/CMakeLists.txt 的接线
AGENTS.md / doc/tasks/**
```

## 4. 测试（已由 AI 写好，当前应为红）

`tests/job/test_mt_queue.cpp` 包含 10 个用例：

1. 单线程 FIFO（含空队列 `try_pop` 返回 false、`close()` 后 `closed()`）；
2. move-only `unique_ptr`；
3. `wait_pop` 确实阻塞，直到 push；
4. `close` 唤醒单个等待者并返回 false；
5. `close` 唤醒**所有**等待者（4 个）——抓 `notify_one` / `notify_all` 混淆；
6. `close` 后仍能 drain 已入队元素；
7. 4 producers / 4 consumers 并发计数与求和；
8. `close` 后 `push` 抛 `std::logic_error`；
9. `close` 后 `push` move-only 值同样抛 `std::logic_error`，且元素不入队；
10. `closed()` / `size()` 状态与 `close()` 幂等。

## 5. 验收

```bash
cmake --build --preset debug-vk
./build/debug-vk/bin/YrTests_job
ctest --test-dir build/debug-vk -R mt_queue
```

> `-R` 匹配的是**用例名**不是 target 名：`-R YrTests_job` 一条都匹配不到（会报 `No tests were found`），`catch_discover_tests` 把每个 `TEST_CASE` 注册成独立 ctest 用例。

要求：

- 全部 10 条 `mt_queue` 用例通过；
- ASan 构建/运行干净；
- 线程测试可重复运行，不出现死锁/丢元素；
- `check_format.sh` 通过。

直接跑 `./build/debug-vk/bin/YrTests_job` 时，若实现“阻塞但不唤醒”，丢唤醒的用例会挂住（无法用断言表达）；用 ctest 跑则单条 30s 超时失败、其余用例正常出结果。

有 TSan 环境时建议额外跑一次（当前 CMake preset 未提供 tsan）。

## 6. 明确不做

- 有界队列 / 阻塞 push / 超时 pop；
- 优先级队列；
- 批量 pop；
- 取消单个元素；
- `JobSystem` 本身（M10-a 只做队列）。

## 7. Q&A / 决策记录

> 设计问答的结论回填到这里；不要只留在聊天里。

- 2026-11-14：`wait_pop` 不接收外部 stop flag，停止统一走 `close()`；这样不会出现“flag 变了但没人 notify”的丢唤醒。
- 2026-11-14：`close()` 后允许 drain 已入队元素；队列空后 `wait_pop` 返回 false。
- 2026-11-14：`push` 在 close 后抛 `std::logic_error`，视为编程错误。
- 2026-11-14：用例按实际 `TEST_CASE` 重排为 10 条（原“空队列 `try_pop` 返回 false”并入单线程 FIFO，不单独占一条）。
- 2026-11-14：新增用例 5“close 唤醒所有等待者”。丢唤醒无法用断言表达：实现若把 `close()` 的 `notify_all` 写成 `notify_one`，多余等待者会永久阻塞在 `join`。用例先用 2s 有界自旋打印可读断言，`tests/job/CMakeLists.txt` 再给 ctest 设 `TIMEOUT 30`，把挂住转成超时失败。直接跑 `./build/debug-vk/bin/YrTests_job` 时该用例仍可能挂住，属预期（用 ctest 跑）。
- 2026-11-14：验收命令从 `-R YrTests_job` 改为 `-R mt_queue`。`catch_discover_tests` 注册的是用例名，target 名匹配不到任何测试；若想按 target 收敛，需要另加 label，暂不做。
- 2026-11-14：新增用例 9“close 后 push move-only 值也抛”。目的不是测异常类型，而是确认 `push(T)` 的拒绝路径不要求 `T` 可拷贝、被拒元素不入队。
- 2026-11-14：`wait_pop` 的“确实阻塞”判据保持弱证明（`entered` 置位 + 10ms + `returned == false`），不做严格阻塞点证明。理由：严格证明需要注入 hook 或探测线程状态，成本大于收益，且易 flaky（R5）。
- 2026-11-14：并发 `size()` / `closed()`、多线程同时 `close()` 的幂等压力暂不测（R3）。前者只有 TSan 能抓到，当前 preset 未提供；等引入 tsan preset 再补。
- 2026-11-14：TSan 验证通过：手工 `build/tsan`（`-fsanitize=thread`）+ `setarch $(uname -m) -R ./build/tsan/bin/YrTests_job`，51 assertions / 10 cases 全绿。当前容器直接跑 TSan 会报 `unexpected memory mapping`，需要 `setarch -R` 关闭 ASLR。
