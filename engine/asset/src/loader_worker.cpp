#include "loader_worker.h"

#include <cstddef>
#include <exception>
#include <utility>
#include <vector>

namespace yr::asset {

  LoaderWorker::LoaderWorker(IFileAccess& access) : access_(&access), thread_(&LoaderWorker::run, this) {}

  LoaderWorker::~LoaderWorker() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void LoaderWorker::submit(LoadRequest request) {
    {
      std::lock_guard lock(mutex_);
      if (stopping_) {
        return;
      }
      queue_.push_back(std::move(request));
    }
    cv_.notify_one();
  }

  std::vector<LoadCompletion> LoaderWorker::takeCompleted(std::size_t maxCount) {
    std::vector<LoadCompletion> out;
    if (maxCount == 0) {
      return out;
    }

    std::lock_guard lock(mutex_);
    const std::size_t count = maxCount < completed_.size() ? maxCount : completed_.size();
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      out.push_back(std::move(completed_[i]));
    }
    completed_.erase(completed_.begin(), completed_.begin() + static_cast<std::ptrdiff_t>(count));
    return out;
  }

  void LoaderWorker::run() {
    for (;;) {
      LoadRequest request;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (stopping_ && queue_.empty()) {
          break;
        }
        request = std::move(queue_.front());
        queue_.pop_front();
      }

      LoadCompletion completion;
      completion.uid = request.uid;
      completion.resPath = request.resPath;
      completion.kind = request.kind;
      try {
        if (access_ == nullptr) {
          completion.error = "LoaderWorker 未绑定 IFileAccess";
        } else if (!access_->readFile(request.resPath, completion.payload, completion.error)) {
          completion.payload.clear();
          if (completion.error.empty()) {
            completion.error = "LoaderWorker 读取文件失败";
          }
        }
      } catch (const std::exception& exception) {
        completion.payload.clear();
        completion.error = exception.what();
      } catch (...) {
        completion.payload.clear();
        completion.error = "LoaderWorker 读取文件时发生未知异常";
      }

      std::lock_guard lock(mutex_);
      completed_.push_back(std::move(completion));
    }
  }

} // namespace yr::asset
