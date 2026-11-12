#include <yr/asset/async_loader.h>

#include <utility>

namespace yr::asset {

  AsyncLoader::AsyncLoader(ILoadBackend& backend) : backend_(&backend) {}

  AssetUID AsyncLoader::requestLoad(std::string_view resPath, ResourceKind kind) {
    const AssetUID uid = AssetUID::fromPath(resPath);
    if (!uid.valid()) {
      return AssetUID{};
    }

    auto it = entries_.find(uid);
    if (it != entries_.end()) {
      Entry& entry = it->second;
      if (entry.state == LoadState::kLoading || entry.state == LoadState::kReady) {
        return uid;
      }
      entry.resPath = std::string(resPath);
      entry.kind = kind;
      entry.state = LoadState::kLoading;
      entry.error.clear();
      backend_->submit(LoadRequest{uid, entry.resPath, kind});
      return uid;
    }

    const auto inserted = entries_.emplace(uid, Entry{std::string(resPath), kind, LoadState::kLoading, {}});
    backend_->submit(LoadRequest{uid, inserted.first->second.resPath, kind});
    return uid;
  }

  LoadState AsyncLoader::state(AssetUID uid) const {
    const auto it = entries_.find(uid);
    return it == entries_.end() ? LoadState::kUnloaded : it->second.state;
  }

  std::string AsyncLoader::error(AssetUID uid) const {
    const auto it = entries_.find(uid);
    return it == entries_.end() ? std::string{} : it->second.error;
  }

  std::vector<LoadCompletion> AsyncLoader::pump(std::size_t maxCompleted) {
    if (maxCompleted == 0) {
      return {};
    }

    std::vector<LoadCompletion> completed = backend_->takeCompleted(maxCompleted);
    std::vector<LoadCompletion> accepted;
    accepted.reserve(completed.size());
    for (LoadCompletion& completion : completed) {
      const auto it = entries_.find(completion.uid);
      if (it == entries_.end()) {
        continue;
      }
      Entry& entry = it->second;
      if (entry.state != LoadState::kLoading) {
        continue;
      }
      if (!completion.error.empty()) {
        entry.state = LoadState::kFailed;
        entry.error = completion.error;
      } else {
        entry.state = LoadState::kReady;
        entry.error.clear();
      }
      accepted.push_back(std::move(completion));
    }
    return accepted;
  }

  void AsyncLoader::forget(AssetUID uid) {
    entries_.erase(uid);
  }

  void AsyncLoader::markFailed(AssetUID uid, std::string error) {
    const auto it = entries_.find(uid);
    if (it == entries_.end()) {
      return;
    }
    it->second.state = LoadState::kFailed;
    it->second.error = std::move(error);
  }

  void AsyncLoader::reset() {
    entries_.clear();
  }

} // namespace yr::asset
