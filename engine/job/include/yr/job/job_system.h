#pragma once

#include "yr/job/mt_queue.h"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace yr::job {

  // JobSystem written by yoyorm
  class JobSystem final {
  public:
    explicit JobSystem(std::size_t workerCount = std::thread::hardware_concurrency());
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    // workerCount == 0 时在调用线程内联执行。
    void submit(std::function<void()> task);

    // 等待当前已提交的任务全部完成。
    void waitAll();

    [[nodiscard]] std::size_t workerCount() const noexcept;

  private:
    std::atomic<std::size_t> workerCount_ = 0;
    std::size_t pendings_ = 0;
    std::condition_variable cv_;
    std::mutex mutex_;
    yr::job::MtQueue<std::function<void()>> queue_;
    std::vector<std::jthread> workers_;
  };

} // namespace yr::job
