#pragma once

#include <cstddef>
#include <utility>

namespace yr::job {

  // M10-a 手写契约：线程安全 FIFO 队列。
  //
  // 所有权：队列拥有入队元素；调用方通过 T& out 取回。
  // 线程：所有公开方法可并发调用。
  // 停止：close() 幂等；close 后 push 抛 std::logic_error；
  //       wait_pop 在队列 drain 完后返回 false。
  //
  // TODO(user): 实现本类。当前只是让测试可编译的占位桩；
  //             需要什么同步原语（mutex / condition_variable / deque）由你决定。
  template <typename T> class MtQueue {
  public:
    MtQueue() = default;
    ~MtQueue() = default;

    MtQueue(const MtQueue&) = delete;
    MtQueue& operator=(const MtQueue&) = delete;
    MtQueue(MtQueue&&) = delete;
    MtQueue& operator=(MtQueue&&) = delete;

    void push(T /*value*/) {
      // TODO(user): 入队并 notify_one；若已 close 抛 std::logic_error。
    }

    [[nodiscard]] bool try_pop(T& /*out*/) {
      // TODO(user): 非阻塞弹出一个元素；空队列返回 false。
      return false;
    }

    [[nodiscard]] bool wait_pop(T& /*out*/) {
      // TODO(user): 阻塞直到有元素或 close 且为空；不要 busy wait。
      return false;
    }

    void close() {
      // TODO(user): 幂等关闭，notify_all。
    }

    [[nodiscard]] bool closed() const noexcept {
      // TODO(user): 返回关闭状态。
      return false;
    }

    [[nodiscard]] std::size_t size() const {
      // TODO(user): 返回当前元素数量。
      return 0;
    }
  };

} // namespace yr::job
