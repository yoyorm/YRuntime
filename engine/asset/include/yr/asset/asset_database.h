#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include <yr/asset/resource.h>
#include <yr/object/ref.h>

namespace yr::asset {

  struct AssetStats {
    std::size_t resident_count = 0;
    std::uint64_t cpu_bytes = 0;
    std::uint64_t registered = 0;
    std::uint64_t evicted = 0;
    std::uint64_t loads = 0;
    std::uint64_t cache_hits = 0;
    std::uint64_t cache_misses = 0;
    std::uint64_t failed_loads = 0;
  };

  struct GcStats {
    std::size_t evicted = 0;
    std::size_t passes = 0;
  };

  // 已加载资源的 UID -> 强引用缓存，负责驻留统计与 fixpoint GC。
  // 仅允许主线程使用：ObjectDB、RefCounted 的释放路径和本类都没有跨线程同步。
  class AssetDatabase final {
  public:
    explicit AssetDatabase(std::string assetRoot);
    ~AssetDatabase();

    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;
    AssetDatabase(AssetDatabase&&) = delete;
    AssetDatabase& operator=(AssetDatabase&&) = delete;

    // 注册已加载资源。UID 必须有效；同一 UID 只能对应同一对象。
    [[nodiscard]] bool store(obj::Ref<Resource> resource);

    [[nodiscard]] bool contains(AssetUID uid) const noexcept;
    [[nodiscard]] obj::Ref<Resource> find(AssetUID uid) const;

    template <typename T> [[nodiscard]] obj::Ref<T> findAs(AssetUID uid) const {
      obj::Ref<Resource> resource = find(uid);
      auto* typed = dynamic_cast<T*>(resource.get());
      return typed != nullptr ? obj::Ref<T>(typed) : obj::Ref<T>{};
    }

    [[nodiscard]] std::string pathFor(AssetUID uid) const;

    // 只有 refcount()==1（只剩 DB）时才允许；被场景/材质使用时返回 false。
    [[nodiscard]] bool evict(AssetUID uid);

    // 反复收集仅剩 DB 持有的资源，直到一轮没有任何候选。
    // 当前资源图是 DAG：Material 析构释放贴图后，下一轮才会看到贴图只剩 DB。
    [[nodiscard]] GcStats collectGarbage();
    [[nodiscard]] AssetStats stats() const;

    void clear();

    [[nodiscard]] const std::string& assetRoot() const noexcept;

  private:
    struct Entry {
      std::string sourcePath;
      obj::Ref<Resource> resource;
    };

    std::string assetRoot_;
    std::unordered_map<AssetUID, Entry> cache_;
    AssetStats counters_{}; // 只使用 registered/evicted/loads/hits/misses/failed
  };

} // namespace yr::asset