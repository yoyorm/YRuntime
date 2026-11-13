#include <yr/asset/asset_database.h>

#include <exception>
#include <utility>
#include <vector>

#include <yr/asset/async_loader.h>
#include <yr/asset/disk_file_access.h>
#include <yr/asset/texture_loader.h>
#include <yr/core/assert.h>
#include <yr/core/log.h>

#include "gltf_mesh_builder.h"
#include "loader_worker.h"

namespace yr::asset {

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

  GltfLoadResult AssetDatabase::loadGltfScene(std::string_view resPath, const GltfLoadOptions& options) {
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

    if (options.deferTextures && asyncLoader_ != nullptr) {
      // 直接委托 AsyncLoader，避免 AssetDatabase::requestLoad 重复计入 loads。
      for (const GltfTextureRequest& request : result.pendingTextures) {
        const AssetUID uid = asyncLoader_->requestLoad(request.resPath, ResourceKind::kTexture, request.colorSpace,
                                                       request.maxDimension);
        YR_ASSERT_MSG(uid == request.uid, "glTF pending texture 的 UID 不一致");
        (void)uid;
      }
    }

    if (options.deferMeshes) {
      for (GltfDeferredMesh& job : result.pendingMeshes) {
        if (!job.uid.valid() || job.source == nullptr) {
          continue;
        }
        if (!pendingMeshUids_.insert(job.uid).second) {
          continue;
        }
        pendingMeshes_.push_back(std::move(job));
      }
      result.pendingMeshes.clear(); // DB 接管
    }
    return result;
  }

  AssetUID AssetDatabase::requestLoad(std::string_view resPath, ResourceKind kind, TextureColorSpace colorSpace,
                                      std::uint32_t maxDimension) {
    const AssetUID uid = AssetUID::fromPath(resPath);
    if (!uid.valid()) {
      return AssetUID{};
    }

    const auto it = cache_.find(uid);
    if (it != cache_.end()) {
      if (kind != ResourceKind::kUnknown && it->second.resource->kind() != kind) {
        YR_LOG_ERROR("asset.db", "requestLoad: kind mismatch " + uid.toString());
        return AssetUID{};
      }
      if (it->second.resource->state() == LoadState::kReady) {
        return uid; // 已在 cache 中，无需重新 submit。
      }
    }

    if (asyncLoader_ == nullptr) {
      return AssetUID{};
    }

    // AsyncLoader 会合并重复请求；只有真正进入加载的请求才计入 loads。
    const LoadState previous = asyncLoader_->state(uid);
    const AssetUID requested = asyncLoader_->requestLoad(resPath, kind, colorSpace, maxDimension);
    if (requested.valid() && previous != LoadState::kLoading && previous != LoadState::kReady) {
      ++counters_.loads;
    }
    return requested;
  }

  LoadState AssetDatabase::state(AssetUID uid) const {
    const auto it = cache_.find(uid);
    if (it != cache_.end()) {
      return it->second.resource->state();
    }
    return asyncLoader_ != nullptr ? asyncLoader_->state(uid) : LoadState::kUnloaded;
  }

  std::string AssetDatabase::error(AssetUID uid) const {
    const auto it = cache_.find(uid);
    if (it != cache_.end()) {
      if (it->second.resource->state() == LoadState::kFailed) {
        return it->second.resource->error();
      }
      if (it->second.resource->state() == LoadState::kReady) {
        return {};
      }
      // kLoading/kUnloaded：可能还没安装，回退到 AsyncLoader 的错误。
    }
    return asyncLoader_ != nullptr ? asyncLoader_->error(uid) : std::string{};
  }

  void AssetDatabase::pump(std::size_t maxTextureCompletions, std::size_t maxMeshVertices) {
    if (maxTextureCompletions == 0) {
      return;
    }

    if (asyncLoader_ != nullptr) {
      for (LoadCompletion& completion : asyncLoader_->pump(maxTextureCompletions)) {
        const AssetUID uid = completion.uid;

        if (!completion.error.empty()) {
          // AsyncLoader 已置 kFailed；cache 中尚未安装的 placeholder 需要同步。
          const auto it = cache_.find(uid);
          if (it != cache_.end() && it->second.resource->state() == LoadState::kLoading) {
            (void)it->second.resource->markFailed(completion.error);
          }
          continue;
        }

        if (completion.kind != ResourceKind::kTexture) {
          const std::string text = "AssetDatabase::pump: 仅支持 Texture 异步加载";
          const auto it = cache_.find(uid);
          if (it != cache_.end() && it->second.resource->state() == LoadState::kLoading) {
            (void)it->second.resource->markFailed(text);
          }
          ++counters_.failed_loads;
          asyncLoader_->markFailed(uid, text);
          continue;
        }

        // 复用 glTF defer 放进 cache_ 的 placeholder；只有缺失时才新建。
        obj::Ref<Texture> texture;
        bool created = false;
        const auto existing = cache_.find(uid);
        if (existing != cache_.end()) {
          texture = obj::Ref<Texture>(dynamic_cast<Texture*>(existing->second.resource.get()));
          if (!texture.valid()) {
            const std::string text = "AssetDatabase::pump: UID 已被非 Texture 资源占用";
            if (existing->second.resource->state() == LoadState::kLoading) {
              (void)existing->second.resource->markFailed(text);
            }
            ++counters_.failed_loads;
            asyncLoader_->markFailed(uid, text);
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

        DecodedTexture decoded;
        std::string error;
        if (!decodeRgba8(completion.payload.data(), completion.payload.size(), completion.maxDimension, decoded,
                         error)) {
          (void)texture->markFailed(error);
          ++counters_.failed_loads;
          asyncLoader_->markFailed(uid, error);
          continue;
        }

        texture->width = decoded.width;
        texture->height = decoded.height;
        texture->rgba8 = std::move(decoded.rgba8);
        texture->colorSpace = completion.colorSpace;

        if (!texture->markReady()) {
          const std::string text = "AssetDatabase::pump: Texture 状态转换失败";
          if (texture->state() == LoadState::kLoading) {
            (void)texture->markFailed(text);
          }
          ++counters_.failed_loads;
          asyncLoader_->markFailed(uid, text);
          continue;
        }

        if (created && !store(texture)) {
          ++counters_.failed_loads;
          asyncLoader_->markFailed(uid, "AssetDatabase::pump: 注册 Texture 失败");
        }
      }
    }

    if (maxMeshVertices > 0) {
      pumpMeshes(maxMeshVertices);
    }
  }

  void AssetDatabase::pumpMeshes(std::size_t maxMeshVertices) {
    std::size_t builtVertices = 0;
    std::size_t builtJobs = 0;

    while (!pendingMeshes_.empty()) {
      const GltfDeferredMesh& front = pendingMeshes_.front();
      if (builtJobs > 0 && builtVertices + front.vertexCount > maxMeshVertices) {
        break;
      }

      GltfDeferredMesh job = std::move(pendingMeshes_.front());
      pendingMeshes_.pop_front();
      pendingMeshUids_.erase(job.uid);

      ++builtJobs;
      builtVertices += job.vertexCount;

      const auto it = cache_.find(job.uid);
      if (it == cache_.end()) {
        continue; // evict/GC 后不要复活
      }

      auto* mesh = dynamic_cast<Mesh*>(it->second.resource.get());
      if (mesh == nullptr || mesh->state() != LoadState::kLoading) {
        continue;
      }

      std::string error;
      bool ok = false;
      try {
        ok = buildGltfMesh(job, *mesh, error);
      } catch (const std::exception& exception) {
        error = exception.what();
      } catch (...) {
        error = "mesh 构建时发生未知异常";
      }

      if (!ok || !mesh->markReady()) {
        if (mesh->state() == LoadState::kLoading) {
          (void)mesh->markFailed(error.empty() ? "mesh 构建失败" : error);
        }
        ++counters_.failed_loads;
      }
    }
  }

  void AssetDatabase::cancelPendingMesh(AssetUID uid) {
    if (pendingMeshUids_.erase(uid) == 0) {
      return;
    }
    std::erase_if(pendingMeshes_, [uid](const GltfDeferredMesh& job) { return job.uid == uid; });
  }

  bool AssetDatabase::evict(AssetUID uid) {
    const auto it = cache_.find(uid);
    if (it == cache_.end() || it->second.resource->refcount() != 1) {
      return false;
    }

    cache_.erase(it);
    ++counters_.evicted;
    cancelPendingMesh(uid);
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

      // 场景释放后立即丢弃 job，尽快释放 job 持有的 GltfSourceData。
      std::unordered_set<AssetUID> removed(victims.begin(), victims.end());
      std::erase_if(pendingMeshes_, [&removed](const GltfDeferredMesh& job) { return removed.contains(job.uid); });
      for (AssetUID uid : removed) {
        pendingMeshUids_.erase(uid);
      }

      ++gc.passes;
      gc.evicted += victims.size();
    }

    return gc;
  }

  AssetStats AssetDatabase::stats() const {
    AssetStats result = counters_;
    result.resident_count = cache_.size();
    result.pending_meshes = pendingMeshes_.size();
    result.cpu_bytes = 0;
    for (const auto& [uid, entry] : cache_) {
      result.cpu_bytes += entry.resource->cpuBytes();
    }
    return result;
  }

  void AssetDatabase::clear() {
    pendingMeshes_.clear();
    pendingMeshUids_.clear();
    cache_.clear();
    if (asyncLoader_ != nullptr) {
      asyncLoader_->reset(); // 避免状态表与 cache_ 分叉；迟到的 completion 会被丢弃。
    }
  }

  const std::string& AssetDatabase::assetRoot() const noexcept {
    return assetRoot_;
  }

} // namespace yr::asset