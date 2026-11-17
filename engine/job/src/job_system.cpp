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
  };

  bool TaskHandle::ready() const noexcept {
    if (state_) {
      return state_->completed.load();
    }
    return false;
  }

  void TaskHandle::wait() const {
    if (!state_) {
      return;
    }
    if (state_->completed.load()) {
      return;
    }
    owner_->waitFor(state_);
  }

  void JobSystem::waitFor(const std::shared_ptr<TaskHandle::State>& state) {
    while (1) {
      if (state->completed.load())
        return;
      Job job;
      if (queue_.try_pop(job)) {
        finishTask_(std::move(job));
        continue;
      }

      std::unique_lock lock{mutex_};
      cv_.wait(lock, [&]() { return state->completed.load() || queue_.size() > 0; });
    }
  }

  JobSystem::JobSystem(std::size_t workerCount) : workerCount_(workerCount) {
    workers_.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; i++) {
      workers_.emplace_back([&]() {
        while (1) {
          Job job;
          if (queue_.wait_pop(job)) {
            finishTask_(std::move(job));
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
      return TaskHandle(state, *this);
    }

    auto state = std::make_shared<TaskHandle::State>();
    {
      std::lock_guard lock{mutex_};
      queue_.push(Job{std::move(task), state});
      pendings_++;
    }

    cv_.notify_all();
    return TaskHandle(state, *this);
  }

  void JobSystem::waitAll() {
    bool finish = false;

    while (1) {
      Job job;

      if (queue_.try_pop(job)) {
        finishTask_(std::move(job));
      } else {
        std::lock_guard lock{mutex_};
        finish = !pendings_;
      }

      if (finish) {
        return;
      } else {
        std::unique_lock lock{mutex_};
        cv_.wait(lock, [&]() { return !pendings_ || queue_.size() > 0; });
        continue;
      }
    }
  }

  std::size_t JobSystem::workerCount() const noexcept {
    return workerCount_;
  }

  void JobSystem::finishTask_(Job job) {
    job.task();
    {
      std::lock_guard lock{mutex_};
      job.state->completed = true;
      pendings_--;
    }
    cv_.notify_all();
  }

} // namespace yr::job
