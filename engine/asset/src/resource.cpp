#include <yr/asset/resource.h>

#include <utility>

#include <yr/core/assert.h>

namespace yr::asset {

  Resource::Resource(ResourceKind kind) noexcept : kind_(kind) {}

  void Resource::setSourcePath(std::string path) {
    sourcePath_ = std::move(path);
  }

  bool Resource::markLoading() noexcept {
    if (state_ == LoadState::kUnloaded || state_ == LoadState::kFailed) {
      state_ = LoadState::kLoading;
      error_.clear();
      return true;
    }
    YR_ASSERT_MSG(false, "Resource::markLoading: invalid state transition");
    return false;
  }

  bool Resource::markReady() noexcept {
    if (state_ == LoadState::kLoading) {
      state_ = LoadState::kReady;
      error_.clear();
      return true;
    }
    YR_ASSERT_MSG(false, "Resource::markReady: invalid state transition");
    return false;
  }

  bool Resource::markFailed(std::string error) {
    if (state_ == LoadState::kLoading) {
      state_ = LoadState::kFailed;
      error_ = std::move(error);
      return true;
    }
    YR_ASSERT_MSG(false, "Resource::markFailed: invalid state transition");
    return false;
  }

} // namespace yr::asset