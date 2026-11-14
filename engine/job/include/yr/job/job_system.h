#pragma once

#include <cstddef>
#include <functional>
#include <thread>

namespace yr::job {

  // M10-a-2 手写契约：固定 worker 数的任务系统。
  //
  // 所有权：JobSystem 拥有 worker 线程与队列；任务由 std::function 持有。
  // 生命周期：析构会 drain 已提交任务并 join worker；不得在任务执行中销毁。
  // 线程：submit 可从多个线程/worker 内调用；waitAll 只允许从非 worker 调用。
  //
  // TODO(user): 实现本类。MtQueue 已完成，直接复用。
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

    // 等待当前已提交（含任务内继续提交）的任务全部完成。
    void waitAll();

    [[nodiscard]] std::size_t workerCount() const noexcept;

  private:
    // TODO(user): 成员由你决定。当前占位让测试可编译。
    std::size_t workerCount_ = 0;
  };

} // namespace yr::job
