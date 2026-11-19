#include "job_load_backend.h"

#include <cstddef>
#include <exception>
#include <utility>
#include <vector>

#include <yr/job/job_system.h>

namespace yr::asset {

  JobLoadBackend::JobLoadBackend(IFileAccess& access, yr::job::JobSystem& jobs, std::size_t maxInFlight,
                                 std::size_t byteBudget)
      : access_(&access), jobs_(&jobs), maxInFlight_(maxInFlight == 0 ? 1 : maxInFlight), byteBudget_(byteBudget) {}

  JobLoadBackend::~JobLoadBackend() {
    std::unique_lock lock(mutex_);
    stopping_ = true;
    pending_.clear(); // 从未提交的请求直接丢弃

    // 任务体持有 this：必须等在飞任务全部结束，否则是 use-after-free。
    cv_.wait(lock, [this] { return inFlight_ == 0; });
  }

  void JobLoadBackend::submit(LoadRequest request) {
    {
      std::lock_guard lock(mutex_);
      if (stopping_) {
        return;
      }
      pending_.push_back(std::move(request));
    }
    drainPending();
  }

  std::vector<LoadCompletion> JobLoadBackend::takeCompleted(std::size_t maxCount) {
    std::vector<LoadCompletion> out;
    if (maxCount > 0) {
      std::lock_guard lock(mutex_);
      const std::size_t count = maxCount < completed_.size() ? maxCount : completed_.size();
      out.reserve(count);

      std::size_t released = 0;
      for (std::size_t i = 0; i < count; ++i) {
        released += completed_[i].decoded.rgba8.size() + completed_[i].payload.size();
        out.push_back(std::move(completed_[i]));
      }
      completed_.erase(completed_.begin(), completed_.begin() + static_cast<std::ptrdiff_t>(count));

      // 取走即释放预算；queuedBytes_ 只会被这里和 execute 修改，恒 >= released。
      queuedBytes_ -= released;
    }

    // 释放掉的额度用来继续提交。
    drainPending();
    return out;
  }

  void JobLoadBackend::drainPending() {
    for (;;) {
      LoadRequest request;
      {
        std::lock_guard lock(mutex_);
        if (stopping_ || inFlight_ >= maxInFlight_ || pending_.empty()) {
          return;
        }
        // 完成队列已达字节预算：先停止提交，等 takeCompleted 释放额度。
        if (byteBudget_ != 0 && queuedBytes_ >= byteBudget_) {
          return;
        }
        request = std::move(pending_.front());
        pending_.pop_front();
        ++inFlight_;
      }

      // 必须在锁外提交：JobSystem(0) 会内联执行，任务体会重新拿 mutex_。
      (void)jobs_->submit([this, request = std::move(request)]() mutable { execute(std::move(request)); });
    }
  }

  void JobLoadBackend::execute(LoadRequest request) {
    LoadCompletion completion;
    completion.uid = request.uid;
    completion.resPath = request.resPath;
    completion.kind = request.kind;
    completion.colorSpace = request.colorSpace;

    try {
      if (access_ == nullptr) {
        completion.error = "JobLoadBackend 未绑定 IFileAccess";
      } else if (!access_->readFile(request.resPath, completion.payload, completion.error)) {
        completion.payload.clear();
        if (completion.error.empty()) {
          completion.error = "JobLoadBackend 读取文件失败";
        }
      } else if (completion.kind == ResourceKind::kTexture) {
        // 解码在 worker 内完成：主线程 pump 只负责安装。
        DecodedTexture decoded;
        std::string decodeError;
        if (!decodeRgba8(completion.payload.data(), completion.payload.size(), request.maxDimension, decoded,
                         decodeError)) {
          completion.error = decodeError.empty() ? "纹理解码失败" : decodeError;
        } else {
          completion.decoded = std::move(decoded);
        }
        std::vector<std::uint8_t>().swap(completion.payload);
      }
    } catch (const std::exception& exception) {
      completion.payload.clear();
      completion.error = exception.what();
    } catch (...) {
      completion.payload.clear();
      completion.error = "JobLoadBackend 读取文件时发生未知异常";
    }

    const std::size_t bytes = completion.decoded.rgba8.size() + completion.payload.size();
    {
      std::lock_guard lock(mutex_);
      completed_.push_back(std::move(completion));
      queuedBytes_ += bytes;
      --inFlight_;
    }
    cv_.notify_all();
  }

} // namespace yr::asset