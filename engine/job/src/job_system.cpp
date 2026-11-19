#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include <yr/job/job_system.h>

namespace yr::job {

  struct TaskHandle::State {
    std::atomic<bool> completed = false;

    // "未就绪"的任务状态
    std::size_t remaining = 0;                      // 尚未完成的前置依赖数
    std::function<void()> task;                     // 未就绪期间保管的任务体
    std::vector<std::shared_ptr<State>> dependents; // 我完成后要唤醒的后继
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
    return submit(std::move(task), std::span<const TaskHandle>{});
  }

  TaskHandle JobSystem::submit(std::function<void()> task, std::span<const TaskHandle> deps) {
    auto state = std::make_shared<TaskHandle::State>();

    // 内联模式：deps 中的任务在各自 submit 返回前已完成，等价于全部满足。
    if (!workerCount_.load()) {
      task();
      state->completed = true;
      return TaskHandle(state, *this);
    }

    {
      std::lock_guard lock{mutex_};

      // 先记账：即使依赖未满足，它也是"已提交但未完成"的任务，
      // 否则 waitAll 可能在"前置完成、后继尚未入队"的间隙提前返回。
      pendings_++;

      std::size_t remaining = 0;
      for (const TaskHandle& dep : deps) {
        if (!dep.state_ || dep.state_->completed.load()) {
          continue; // 默认空句柄 / 已完成：视为已满足
        }
        remaining++;
        dep.state_->dependents.push_back(state);
      }

      if (remaining == 0) {
        queue_.push(Job{std::move(task), state}); // 已就绪，直接进就绪队列
      } else {
        state->remaining = remaining;
        state->task = std::move(task); // 未就绪：自己保管任务体
      }
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

  void JobSystem::parallelFor(std::size_t count, std::size_t batchSize,
                              std::function<void(std::size_t, std::size_t)> fn) {
    if (count == 0) {
      return; // 不提交任何批次
    }
    if (batchSize == 0) {
      batchSize = 1; // 每个元素一个批次
    }

    // 用除法 + 取余计算批次数，避免 (count + batchSize - 1) 的溢出。
    const std::size_t batches = count / batchSize + (count % batchSize == 0 ? 0 : 1);

    std::vector<TaskHandle> handles;
    handles.reserve(batches);

    for (std::size_t batch = 0; batch < batches; ++batch) {
      const std::size_t begin = batch * batchSize;
      // count - begin < batchSize 避免 begin + batchSize 溢出。
      const std::size_t end = (count - begin < batchSize) ? count : begin + batchSize;
      // fn 按值拷贝进每个批次；move 会让后续批次拿到空 function。
      handles.push_back(submit([fn, begin, end] { fn(begin, end); }));
    }

    // 只等本调用提交的批次，不等全局（不是 waitAll）。
    for (const TaskHandle& handle : handles) {
      handle.wait();
    }
  }

  void JobSystem::finishTask_(Job job) {
    job.task();
    {
      std::lock_guard lock{mutex_};
      job.state->completed = true;
      pendings_--;

      for (const std::shared_ptr<TaskHandle::State>& dependent : job.state->dependents) {
        if (--dependent->remaining == 0) {
          queue_.push(Job{std::move(dependent->task), dependent}); // 最后一个前置完成
        }
      }
      job.state->dependents.clear(); // 释放后继引用
    }
    cv_.notify_all();
  }

} // namespace yr::job
