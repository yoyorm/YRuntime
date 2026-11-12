#include <yr/asset/asset_database.h>

#include <limits>
#include <utility>
#include <vector>

#include <stb_image.h>

#include <yr/asset/async_loader.h>
#include <yr/asset/disk_file_access.h>
#include <yr/core/assert.h>
#include <yr/core/log.h>

#include "loader_worker.h"

namespace yr::asset {

  namespace {

    std::string textureDecodeError(const char* fallback) {
      const char* const reason = stbi_failure_reason();
      return std::string("纹理解码失败：") + (reason != nullptr ? reason : fallback);
    }

  } // namespace

  AssetDatabase::AssetDatabase(std::string assetRoot)
      : assetRoot_(std::move(assetRoot)), fileAccess_(std::make_unique<DiskFileAccess>(assetRoot_)),
        loaderWorker_(std::make_unique<LoaderWorker>(*fileAccess_)),
        asyncLoader_(std::make_unique<AsyncLoader>(*loaderWorker_)) {}

  AssetDatabase::AssetDatabase(std::unique_ptr<IFileAccess> access)
      : fileAccess_(std::move(access)), loaderWorker_(std::make_unique<LoaderWorker>(*fileAccess_)),
        asyncLoader_(std::make_unique<AsyncLoader>(*loaderWorker_)) {}

  AssetDatabase::~AssetDatabase() = default;

  bool AssetDatabase::store(obj::Ref<Resource> resource) {
    if (!resource) {
      return false;
    }

    const AssetUID uid = resource->uid();
    if (!uid.valid()) {
      return false;
    }

    const auto existing = cache_.find(uid);
    if (existing != cache_.end()) {
      if (existing->second.resource.get() == resource.get()) {
        return true;
      }
      YR_LOG_ERROR("asset.db", "AssetDatabase::store: UID 冲突 " + uid.toString());
      return false;
    }

    std::string sourcePath = resource->sourcePath();
    cache_.emplace(uid, Entry{std::move(sourcePath), std::move(resource)});
    ++counters_.registered;
    return true;
  }

  bool AssetDatabase::contains(AssetUID uid) const noexcept {
    return cache_.find(uid) != cache_.end();
  }

  obj::Ref<Resource> AssetDatabase::find(AssetUID uid) const {
    const auto it = cache_.find(uid);
    return it != cache_.end() ? it->second.resource : obj::Ref<Resource>{};
  }

  std::string AssetDatabase::pathFor(AssetUID uid) const {
    const auto it = cache_.find(uid);
    return it != cache_.end() ? it->second.sourcePath : std::string{};
  }

  GltfLoadResult AssetDatabase::loadGltfScene(std::string_view resPath, bool deferTextures) {
    ++counters_.loads;

    std::vector<AssetUID> newUids;
    GltfResourceHooks hooks{
        .find = [this](AssetUID uid) -> obj::Ref<Resource> {
          auto it = cache_.find(uid);
          if (it == cache_.end()) {
            ++counters_.cache_misses;
            return {};
          }
          ++counters_.cache_hits;
          return it->second.resource;
        },
        .store =
            [this, &newUids](obj::Ref<Resource> resource) {
              const AssetUID uid = resource ? resource->uid() : AssetUID{};
              const bool alreadyCached = contains(uid);
              if (!store(std::move(resource))) {
                return false;
              }
              if (!alreadyCached && uid.valid()) {
                newUids.push_back(uid);
              }
              return true;
            },
    };

    const GltfLoadOptions options{.deferTextures = deferTextures};
    GltfLoadResult result = ::yr::asset::loadGltfScene(resPath, assetRoot_, &hooks, &options);
    if (!result.ok) {
      ++counters_.failed_loads;
      for (AssetUID uid : newUids) {
        auto it = cache_.find(uid);
        if (it != cache_.end() && it->second.resource->refcount() == 1) {
          cache_.erase(it);
          if (counters_.registered > 0) {
            --counters_.registered;
          }
        }
      }
      return result;
    }

    if (deferTextures && asyncLoader_ != nullptr) {
      // 直接委托 AsyncLoader，避免 AssetDatabase::requestLoad 重复计入 loads。
      for (const GltfTextureRequest& request : result.pendingTextures) {
        const AssetUID uid = asyncLoader_->requestLoad(request.resPath, ResourceKind::kTexture);
        YR_ASSERT_MSG(uid == request.uid, "glTF pending texture 的 UID 不一致");
        (void)uid;
      }
    }
    return result;
  }

  AssetUID AssetDatabase::requestLoad(std::string_view resPath, ResourceKind kind) {
    if (asyncLoader_ == nullptr) {
      return AssetUID{};
    }

    // AsyncLoader 会合并重复请求；只有真正进入加载的请求才计入 loads。
    const LoadState previous = asyncLoader_->state(AssetUID::fromPath(resPath));
    const AssetUID uid = asyncLoader_->requestLoad(resPath, kind);
    if (uid.valid() && previous != LoadState::kLoading && previous != LoadState::kReady) {
      ++counters_.loads;
    }
    return uid;
  }

  LoadState AssetDatabase::state(AssetUID uid) const {
    return asyncLoader_ != nullptr ? asyncLoader_->state(uid) : LoadState::kUnloaded;
  }

  std::string AssetDatabase::error(AssetUID uid) const {
    return asyncLoader_ != nullptr ? asyncLoader_->error(uid) : std::string{};
  }

  void AssetDatabase::pump(std::size_t maxCompleted) {
    if (maxCompleted == 0 || asyncLoader_ == nullptr) {
      return;
    }

    for (LoadCompletion& completion : asyncLoader_->pump(maxCompleted)) {
      const AssetUID uid = completion.uid;

      if (!completion.error.empty()) {
        continue; // AsyncLoader 已置 kFailed，无需安装。
      }

      if (completion.kind != ResourceKind::kTexture) {
        ++counters_.failed_loads;
        asyncLoader_->markFailed(uid, "AssetDatabase::pump: 仅支持 Texture 异步加载");
        continue;
      }

      // 复用 glTF defer 放进 cache_ 的 placeholder；只有缺失时才新建。
      obj::Ref<Texture> texture;
      bool created = false;
      const auto existing = cache_.find(uid);
      if (existing != cache_.end()) {
        texture = obj::Ref<Texture>(dynamic_cast<Texture*>(existing->second.resource.get()));
        if (!texture.valid()) {
          ++counters_.failed_loads;
          asyncLoader_->markFailed(uid, "AssetDatabase::pump: UID 已被非 Texture 资源占用");
          continue;
        }
        if (texture->state() == LoadState::kUnloaded || texture->state() == LoadState::kFailed) {
          (void)texture->markLoading();
        }
        if (texture->state() != LoadState::kLoading) {
          continue; // 同步路径已加载完成，保留现有内容。
        }
      } else {
        texture = obj::make_ref<Texture>();
        texture->setUid(uid);
        texture->setSourcePath(completion.resPath);
        (void)texture->markLoading();
        created = true;
      }

      if (completion.payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        const std::string text = "纹理解码失败：文件过大";
        (void)texture->markFailed(text);
        ++counters_.failed_loads;
        asyncLoader_->markFailed(uid, text);
        continue;
      }

      int width = 0;
      int height = 0;
      int channels = 0;
      stbi_uc* const pixels = stbi_load_from_memory(
          completion.payload.data(), static_cast<int>(completion.payload.size()), &width, &height, &channels, 4);
      if (pixels == nullptr) {
        const std::string text = textureDecodeError("stbi_load_from_memory 失败");
        (void)texture->markFailed(text);
        ++counters_.failed_loads;
        asyncLoader_->markFailed(uid, text);
        continue;
      }

      if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        const std::string text = "纹理解码失败：图片尺寸无效";
        (void)texture->markFailed(text);
        ++counters_.failed_loads;
        asyncLoader_->markFailed(uid, text);
        continue;
      }

      const std::uint64_t pixelBytes = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
      if (pixelBytes > std::numeric_limits<std::size_t>::max()) {
        stbi_image_free(pixels);
        const std::string text = "纹理解码失败：图片过大";
        (void)texture->markFailed(text);
        ++counters_.failed_loads;
        asyncLoader_->markFailed(uid, text);
        continue;
      }

      texture->width = static_cast<std::uint32_t>(width);
      texture->height = static_cast<std::uint32_t>(height);
      texture->rgba8.assign(pixels, pixels + static_cast<std::size_t>(pixelBytes));
      stbi_image_free(pixels);

      if (!texture->markReady()) {
        ++counters_.failed_loads;
        asyncLoader_->markFailed(uid, "AssetDatabase::pump: Texture 状态转换失败");
        continue;
      }

      if (created && !store(texture)) {
        ++counters_.failed_loads;
        asyncLoader_->markFailed(uid, "AssetDatabase::pump: 注册 Texture 失败");
      }
    }
  }

  bool AssetDatabase::evict(AssetUID uid) {
    const auto it = cache_.find(uid);
    if (it == cache_.end() || it->second.resource->refcount() != 1) {
      return false;
    }

    cache_.erase(it);
    ++counters_.evicted;
    if (asyncLoader_ != nullptr) {
      asyncLoader_->forget(uid);
    }
    return true;
  }

  GcStats AssetDatabase::collectGarbage() {
    GcStats gc;

    while (true) {
      std::vector<AssetUID> victims;
      victims.reserve(cache_.size());
      for (const auto& [uid, entry] : cache_) {
        if (entry.resource->refcount() == 1) {
          victims.push_back(uid);
        }
      }
      if (victims.empty()) {
        break;
      }

      for (const AssetUID uid : victims) {
        cache_.erase(uid);
        ++counters_.evicted;
        if (asyncLoader_ != nullptr) {
          asyncLoader_->forget(uid);
        }
      }
      ++gc.passes;
      gc.evicted += victims.size();
    }

    return gc;
  }

  AssetStats AssetDatabase::stats() const {
    AssetStats result = counters_;
    result.resident_count = cache_.size();
    result.cpu_bytes = 0;
    for (const auto& [uid, entry] : cache_) {
      result.cpu_bytes += entry.resource->cpuBytes();
    }
    return result;
  }

  void AssetDatabase::clear() {
    cache_.clear();
    if (asyncLoader_ != nullptr) {
      asyncLoader_->reset(); // 避免状态表与 cache_ 分叉；迟到的 completion 会被丢弃。
    }
  }

  const std::string& AssetDatabase::assetRoot() const noexcept {
    return assetRoot_;
  }

} // namespace yr::asset