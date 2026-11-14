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

`tests/job/test_mt_queue.cpp` 包含：

1. 单线程 FIFO；
2. move-only `unique_ptr`；
3. 空队列 `try_pop` 返回 false；
4. `wait_pop` 确实阻塞，直到 push；
5. `close` 唤醒等待者并返回 false；
6. `close` 后仍能 drain 已入队元素；
7. 4 producers / 4 consumers 并发计数与求和；
8. `close` 后 `push` 抛 `std::logic_error`；
9. `closed()` / `size()`。

## 5. 验收

```bash
cmake --build --preset debug-vk
./build/debug-vk/bin/YrTests_job
ctest --test-dir build/debug-vk -R YrTests_job
```

要求：

- 全部 `YrTests_job` 用例通过；
- ASan 构建/运行干净；
- 线程测试可重复运行，不出现死锁/丢元素；
- `check_format.sh` 通过。

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
