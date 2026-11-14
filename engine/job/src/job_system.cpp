#include <yr/job/job_system.h>

#include <utility>

namespace yr::job {

  JobSystem::JobSystem(std::size_t workerCount) : workerCount_(workerCount) {
    // TODO(user): workerCount > 0 时启动 worker；0 表示调用线程内联。
  }

  JobSystem::~JobSystem() {
    // TODO(user): close 队列、drain、join worker。
  }

  void JobSystem::submit(std::function<void()> task) {
    // TODO(user): 入队并 notify；workerCount == 0 时直接执行。
    (void)task;
  }

  void JobSystem::waitAll() {
    // TODO(user): 等待 pending 归零（覆盖嵌套提交）。
  }

  std::size_t JobSystem::workerCount() const noexcept {
    return workerCount_;
  }

} // namespace yr::job
