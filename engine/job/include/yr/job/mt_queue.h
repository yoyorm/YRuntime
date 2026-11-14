#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace yr::job {

  // 线程安全队列 written by yoyorm
  template <typename T> class MtQueue {
  public:
    MtQueue() = default;

    // 析构前需要 close() 队列并且保证所有work线程都已经退出
    ~MtQueue() = default;

    MtQueue(const MtQueue&) = delete;
    MtQueue& operator=(const MtQueue&) = delete;
    MtQueue(MtQueue&&) = delete;
    MtQueue& operator=(MtQueue&&) = delete;

    void push(T value) {
      {
        std::lock_guard lock_{mutex_};
        if (is_closed)
          throw std::logic_error("MtQueue has been closed");
        queue_.push_back(std::move(value));
      }
      has_one.notify_one();
    }

    // 非阻塞弹出
    [[nodiscard]] bool try_pop(T& out) {
      std::unique_lock lock_{mutex_};
      if (queue_.empty()) {
        return false;
      }
      out = std::move(queue_.front());
      queue_.pop_front();
      return true;
    }

    [[nodiscard]] bool wait_pop(T& out) {
      std::unique_lock lock_{mutex_};
      has_one.wait(lock_, [&]() { return is_closed || !queue_.empty(); });
      // 当没有关闭且队列空的时候必须等待,直到关闭或者有元素可以取出

      if (queue_.empty()) { // 关闭且队列空
        return false;
      }

      out = std::move(queue_.front());
      queue_.pop_front();
      return true;
    }

    void close() {
      {
        std::lock_guard lock_{mutex_};
        is_closed = true;
      }
      has_one.notify_all();
      // 通知所有的等待的waitpop结束等待
    }

    [[nodiscard]] bool closed() const noexcept { return is_closed; }

    [[nodiscard]] std::size_t size() const {
      std::lock_guard lock{mutex_};
      return queue_.size();
    }

  private:
    std::deque<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable has_one;
    std::atomic<bool> is_closed = false; // 防止修改竞争
  };

} // namespace yr::job
