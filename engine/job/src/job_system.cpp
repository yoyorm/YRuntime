#include <cstddef>
#include <functional>
#include <mutex>
#include <utility>
#include <yr/job/job_system.h>

namespace yr::job {

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

  void JobSystem::submit(std::function<void()> task) {
    // workerCount == 0 时直接执行。
    if (!workerCount_.load()) {
      task();
      return;
    }

    std::lock_guard lock{mutex_};
    queue_.push(std::move(task));
    pendings_++;
  }

  void JobSystem::waitAll() {
    std::unique_lock lock{mutex_};
    cv_.wait(lock, [&]() { return !pendings_; });
  }

  std::size_t JobSystem::workerCount() const noexcept {
    return workerCount_;
  }

} // namespace yr::job
