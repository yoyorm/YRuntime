#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <yr/asset/asset_uid.h>
#include <yr/asset/resource.h>
#include <yr/asset/texture_loader.h>

namespace yr::asset {

  // JobLoadBackend 完成队列的默认字节预算
  inline constexpr std::size_t kDefaultLoadByteBudget = 256u * 1024u * 1024u;
  // 同时在飞（已提交未完成）的解码任务数。实测 4→8：解码墙钟 9.23s → 5.62s（12 线程）。
  inline constexpr std::size_t kDefaultMaxInFlight = 8;
  // 主线程每帧最多安装的纹理数。小纹理/1K 缓存下 1 张/帧会把 ready 卡在 ~4.8s 地板。
  inline constexpr std::size_t kDefaultTextureCompletionsPerPump = 4;

  struct LoadRequest {
    AssetUID uid;
    std::string resPath;
    ResourceKind kind = ResourceKind::kUnknown;
    TextureColorSpace colorSpace = TextureColorSpace::kSrgb;
    std::uint32_t maxDimension = 0;
  };

  struct LoadCompletion {
    AssetUID uid;
    std::string resPath;
    ResourceKind kind = ResourceKind::kUnknown;
    // 非纹理（kUnknown/kMesh）的原始字节；纹理路径解码后此字段为空。
    std::vector<std::uint8_t> payload;
    DecodedTexture decoded; // 纹理：worker 内 decodeRgba8 的结果（含下采样）
    std::string error;
    TextureColorSpace colorSpace = TextureColorSpace::kSrgb;
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

    [[nodiscard]] AssetUID requestLoad(std::string_view resPath, ResourceKind kind = ResourceKind::kUnknown,
                                       TextureColorSpace colorSpace = TextureColorSpace::kSrgb,
                                       std::uint32_t maxDimension = 0);

    [[nodiscard]] LoadState state(AssetUID uid) const;
    [[nodiscard]] std::string error(AssetUID uid) const;

    // 最多消费 maxCompleted 个完成项；成功项状态置 kReady，失败项置 kFailed。
    [[nodiscard]] std::vector<LoadCompletion> pump(std::size_t maxCompleted = 1);

    // 清掉旧 UID 的状态
    void forget(AssetUID uid);

    // 安装 Resource 失败时调用
    void markFailed(AssetUID uid, std::string error);

    void reset();

  private:
    struct Entry {
      std::string resPath;
      ResourceKind kind = ResourceKind::kUnknown;
      TextureColorSpace colorSpace = TextureColorSpace::kSrgb;
      std::uint32_t maxDimension = 0;
      LoadState state = LoadState::kLoading;
      std::string error;
    };

    ILoadBackend* backend_ = nullptr;
    std::unordered_map<AssetUID, Entry> entries_;
  };

} // namespace yr::asset
