#include <yr/asset/asset_database.h>

#include <utility>
#include <vector>

#include <yr/core/log.h>

namespace yr::asset {

  AssetDatabase::AssetDatabase(std::string assetRoot) : assetRoot_(std::move(assetRoot)) {}

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

  GltfLoadResult AssetDatabase::loadGltfScene(std::string_view resPath) {
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

    GltfLoadResult result = ::yr::asset::loadGltfScene(resPath, assetRoot_, &hooks);
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
    }
    return result;
  }

  bool AssetDatabase::evict(AssetUID uid) {
    const auto it = cache_.find(uid);
    if (it == cache_.end() || it->second.resource->refcount() != 1) {
      return false;
    }

    cache_.erase(it);
    ++counters_.evicted;
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
  }

  const std::string& AssetDatabase::assetRoot() const noexcept {
    return assetRoot_;
  }

} // namespace yr::asset