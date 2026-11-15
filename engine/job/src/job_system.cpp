#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <yr/job/job_system.h>

namespace yr::job {

  struct TaskHandle::State {
    std::atomic<bool> completed = false;
    std::condition_variable cv;
    std::mutex mutex;
  };

  bool TaskHandle::ready() const noexcept {
    if (state_) {
      return state_->completed.load();
    }
    return false;
  }

  void TaskHandle::wait() const {
    if (state_) {
      std::unique_lock lock{state_->mutex};
      state_->cv.wait(lock, [&]() { return state_->completed.load(); });
    }
  }

  JobSystem::JobSystem(std::size_t workerCount) : workerCount_(workerCount) {
    workers_.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; i++) {
      workers_.emplace_back([&]() {
        while (1) {
          std::function<void()> task;
          bool finish = false;
          if (queue_.wait_pop(task)) {
            task();
            {
              std::lock_guard lock{mutex_};
              pendings_--;
              finish = !pendings_;
            }
            if (finish)
              cv_.notify_all();
          } else {
            break;
          }
        }
      });
    }
  }

  JobSystem::~JobSystem() {
    waitAll();
    queue_.close();
  }

  TaskHandle JobSystem::submit(std::function<void()> task) {
    // workerCount == 0 时直接执行。
    if (!workerCount_.load()) {
      task();
      auto state = std::make_shared<TaskHandle::State>();
      state->completed = true;
      return TaskHandle(state);
    }

    auto state = std::make_shared<TaskHandle::State>();
    auto task_with_state = [state, task = std::move(task)]() {
      task();
      {
        std::lock_guard lock{state->mutex};
        state->completed.store(true);
      }
      state->cv.notify_all();
    };

    std::lock_guard lock{mutex_};

    queue_.push(std::move(task_with_state));
    pendings_++;
    return TaskHandle(state);
  }

  void JobSystem::waitAll() {
    std::unique_lock lock{mutex_};
    cv_.wait(lock, [&]() { return !pendings_; });
  }

  std::size_t JobSystem::workerCount() const noexcept {
    return workerCount_;
  }

} // namespace yr::job
