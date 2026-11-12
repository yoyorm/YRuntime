#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <yr/asset/asset_uid.h>
#include <yr/asset/resource.h>

namespace yr::asset {

  struct LoadRequest {
    AssetUID uid;
    std::string resPath;
    ResourceKind kind = ResourceKind::kUnknown;
  };

  struct LoadCompletion {
    AssetUID uid;
    ResourceKind kind = ResourceKind::kUnknown;
    std::vector<std::uint8_t> payload;
    std::string error;
  };

  class ILoadBackend {
  public:
    virtual ~ILoadBackend() = default;
    virtual void submit(LoadRequest request) = 0;
    virtual std::vector<LoadCompletion> takeCompleted(std::size_t maxCount) = 0;
  };

  class AsyncLoader {
  public:
    explicit AsyncLoader(ILoadBackend& backend);

    [[nodiscard]] AssetUID requestLoad(std::string_view resPath, ResourceKind kind = ResourceKind::kUnknown);

    [[nodiscard]] LoadState state(AssetUID uid) const;
    [[nodiscard]] std::string error(AssetUID uid) const;

    // 最多消费 maxCompleted 个完成项；成功项状态置 kReady，失败项置 kFailed。
    [[nodiscard]] std::vector<LoadCompletion> pump(std::size_t maxCompleted = 1);

    // 资源被 evict/GC 后调用，清掉旧 UID 的状态；下次 request 会重新提交。
    void forget(AssetUID uid);

    // 安装 Resource 失败时调用（状态已 kReady 但 cache 未建立）。
    void markFailed(AssetUID uid, std::string error);

    void reset();

  private:
    struct Entry {
      std::string resPath;
      ResourceKind kind = ResourceKind::kUnknown;
      LoadState state = LoadState::kLoading;
      std::string error;
    };

    ILoadBackend* backend_ = nullptr;
    std::unordered_map<AssetUID, Entry> entries_;
  };

} // namespace yr::asset
