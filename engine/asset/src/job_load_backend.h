#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

#include <yr/asset/async_loader.h>
#include <yr/asset/file_access.h>

namespace yr::job {
  class JobSystem;
} // namespace yr::job

namespace yr::asset {

  // 资产加载后端：把 LoadRequest 交给 JobSystem 执行（读字节，随后解码），
  // 主线程通过 takeCompleted() 取走完成项。自身不拥有线程。
  class JobLoadBackend final : public ILoadBackend {
  public:
    // access / jobs 非拥有，必须比 JobLoadBackend 活得更久。
    // maxInFlight：同时提交给 JobSystem 的最大任务数（背压）；0 视为 1。
    // byteBudget：completed_ 中 decoded+payload 的字节上限；0 表示不限制。
    explicit JobLoadBackend(IFileAccess& access, yr::job::JobSystem& jobs,
                            std::size_t maxInFlight = kDefaultMaxInFlight,
                            std::size_t byteBudget = kDefaultLoadByteBudget);
    ~JobLoadBackend() override;

    JobLoadBackend(const JobLoadBackend&) = delete;
    JobLoadBackend& operator=(const JobLoadBackend&) = delete;
    JobLoadBackend(JobLoadBackend&&) = delete;
    JobLoadBackend& operator=(JobLoadBackend&&) = delete;

    void submit(LoadRequest request) override;
    [[nodiscard]] std::vector<LoadCompletion> takeCompleted(std::size_t maxCount) override;

  private:
    // 在预算内把 pending_ 提交给 jobs_；调用方必须不持有 mutex_。
    void drainPending();
    // 任务体：读文件（后续在此解码），再把完成项放进 completed_。
    void execute(LoadRequest request);

    IFileAccess* access_ = nullptr;
    yr::job::JobSystem* jobs_ = nullptr;
    std::size_t maxInFlight_ = 4;
    std::size_t byteBudget_ = kDefaultLoadByteBudget;

    std::mutex mutex_;
    std::condition_variable cv_;      // 仅用于析构等待 inFlight_ == 0
    std::deque<LoadRequest> pending_; // 已接收、尚未提交给 JobSystem
    std::vector<LoadCompletion> completed_;
    std::size_t inFlight_ = 0;    // 已提交、尚未完成
    std::size_t queuedBytes_ = 0; // completed_ 中 decoded + payload 的字节和
    bool stopping_ = false;
  };

} // namespace yr::asset