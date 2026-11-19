#pragma once

#include "yr/job/mt_queue.h"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace yr::job {

  // 任务系统运行统计快照（某一时刻的近似值）
  struct JobStats {
    std::size_t submitted = 0;       // 累计 submit 的任务数（含内联执行与等待依赖的任务）
    std::size_t completed = 0;       // 累计完成数
    std::size_t helperCompleted = 0; // 其中由非 worker 线程执行的（内联 / waitAll / wait 帮忙）
    std::size_t peakReadyDepth = 0;  // 就绪队列深度峰值
    std::size_t readyDepth = 0;      // 当前就绪队列深度（近似值）
  };

  // 单个任务的完成句柄。生命周期必须小于JobSystem
  // 可拷贝,所有副本共享同一个完成状态
  class JobSystem;
  class TaskHandle {
  public:
    TaskHandle() = default;

    [[nodiscard]] bool ready() const noexcept;
    void wait() const;

  private:
    struct State;
    explicit TaskHandle(std::shared_ptr<State> state, JobSystem& owner) : state_(std::move(state)), owner_(&owner) {}

    std::shared_ptr<State> state_;
    JobSystem* owner_ = nullptr;

    friend class JobSystem;
  };

  // 不允许任务内调用 waitAll 且 JobSystem必须存活比TaskHandle更久
  class JobSystem final {
  public:
    explicit JobSystem(std::size_t workerCount = std::thread::hardware_concurrency());
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    // workerCount == 0 时在调用线程内联执行。
    // 返回的 TaskHandle 在任务完成后 ready() == true。
    [[nodiscard]] TaskHandle submit(std::function<void()> task);

    // 当 deps 中所有有效任务完成后，task 才进入就绪队列执行。
    [[nodiscard]] TaskHandle submit(std::function<void()> task, std::span<const TaskHandle> deps);
    [[nodiscard]] TaskHandle submit(std::function<void()> task, std::initializer_list<TaskHandle> deps) {
      return submit(std::move(task), std::span<const TaskHandle>{deps.begin(), deps.size()});
    }

    // 等待当前已提交的任务全部完成。
    void waitAll();

    // 把 [0, count) 按 batchSize 切成不重叠区间并发执行；阻塞到本调用提交的批次全部完成。
    // count == 0 直接返回；batchSize == 0 视作 1；只允许非 worker 线程调用。
    // 不提供跨批次归约：fn 若写共享目标，需自行保证无数据竞争且结果与串行一致。
    void parallelFor(std::size_t count, std::size_t batchSize,
                     std::function<void(std::size_t begin, std::size_t end)> fn);

    [[nodiscard]] std::size_t workerCount() const noexcept;

    // 运行统计快照；只允许在同步点（如 waitAll 之后）读取。
    [[nodiscard]] JobStats stats() const;

  private:
    friend TaskHandle;

    struct Job {
      std::function<void()> task;
      std::shared_ptr<TaskHandle::State> state;
    };

    void finishTask_(Job job, bool helped);
    void waitFor(const std::shared_ptr<TaskHandle::State>& state);

    // 必须在持有 mutex_ 时调用：push 之后更新就绪队列深度峰值。
    void noteReadyDepthLocked();

    std::atomic<std::size_t> workerCount_ = 0;
    std::size_t pendings_ = 0;
    std::condition_variable cv_;
    mutable std::mutex mutex_; // stats() 是 const，需要加锁
    yr::job::MtQueue<Job> queue_;
    std::vector<std::jthread> workers_;

    // 统计计数；全部受 mutex_ 保护（stats() 返回快照，不承诺实时精确）。
    std::size_t submitted_ = 0;
    std::size_t completed_ = 0;
    std::size_t helperCompleted_ = 0;
    std::size_t peakReadyDepth_ = 0;
  };

} // namespace yr::job
