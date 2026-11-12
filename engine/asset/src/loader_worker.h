#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <yr/asset/async_loader.h>
#include <yr/asset/file_access.h>

namespace yr::asset {

  class LoaderWorker final : public ILoadBackend {
  public:
    // access 非拥有，必须比 LoaderWorker 活得更久。
    explicit LoaderWorker(IFileAccess& access);
    ~LoaderWorker() override;

    LoaderWorker(const LoaderWorker&) = delete;
    LoaderWorker& operator=(const LoaderWorker&) = delete;
    LoaderWorker(LoaderWorker&&) = delete;
    LoaderWorker& operator=(LoaderWorker&&) = delete;

    void submit(LoadRequest request) override;
    [[nodiscard]] std::vector<LoadCompletion> takeCompleted(std::size_t maxCount) override;

  private:
    void run();

    IFileAccess* access_ = nullptr;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<LoadRequest> queue_;
    std::vector<LoadCompletion> completed_;

    std::thread thread_;
    bool stopping_ = false;
  };

} // namespace yr::asset
