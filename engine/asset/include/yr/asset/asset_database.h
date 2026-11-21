#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <yr/asset/async_loader.h>
#include <yr/asset/file_access.h>
#include <yr/asset/gltf_loader.h>
#include <yr/asset/resource.h>
#include <yr/object/ref.h>

namespace yr::job {
  class JobSystem;
} // namespace yr::job

namespace yr::asset {

  class JobLoadBackend;
  class AsyncLoader;

  struct AssetStats {
    std::size_t resident_count = 0;
    std::uint64_t cpu_bytes = 0;
    std::uint64_t registered = 0;
    std::uint64_t evicted = 0;
    std::uint64_t loads = 0;
    std::uint64_t cache_hits = 0;
    std::uint64_t cache_misses = 0;
    std::uint64_t failed_loads = 0;
    std::size_t pending_meshes = 0;
  };

  struct GcStats {
    std::size_t evicted = 0;
    std::size_t passes = 0;
  };

  // 已加载资源的 UID -> 强引用缓存，负责驻留统计与GC。
  // 仅允许主线程使用,对象构造与安装在主线程。
  class AssetDatabase final {
  public:
    // byteBudget：JobLoadBackend 完成队列的字节预算；0 表示不限制。
    explicit AssetDatabase(std::string assetRoot, yr::job::JobSystem* jobs = nullptr,
                           std::size_t byteBudget = kDefaultLoadByteBudget);
    // access 非空且归本类所有；用于 Memory/Throttled 等注入实现。
    explicit AssetDatabase(std::unique_ptr<IFileAccess> access, yr::job::JobSystem* jobs = nullptr,
                           std::size_t byteBudget = kDefaultLoadByteBudget);
    ~AssetDatabase();

    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;
    AssetDatabase(AssetDatabase&&) = delete;
    AssetDatabase& operator=(AssetDatabase&&) = delete;

    // 注册已加载资源
    [[nodiscard]] bool store(obj::Ref<Resource> resource);

    [[nodiscard]] bool contains(AssetUID uid) const noexcept;
    [[nodiscard]] obj::Ref<Resource> find(AssetUID uid) const;

    template <typename T> [[nodiscard]] obj::Ref<T> findAs(AssetUID uid) const {
      obj::Ref<Resource> resource = find(uid);
      auto* typed = dynamic_cast<T*>(resource.get());
      return typed != nullptr ? obj::Ref<T>(typed) : obj::Ref<T>{};
    }

    [[nodiscard]] std::string pathFor(AssetUID uid) const;

    // 解析 glTF 结构并通过 hooks 复用/注册资源；失败时回滚本次新增的资源。
    // deferTextures：Texture 只注册 kLoading 占位，并通过 AsyncLoader 排队异步加载。
    // deferMeshes：Mesh 只注册 kLoading 占位，job 由本类接管，后续 pumpMeshes 安装。
    // skipTextures：不创建 Texture，也不产生 pendingTextures。
    [[nodiscard]] GltfLoadResult loadGltfScene(std::string_view resPath, const GltfLoadOptions& options = {});

    // 异步加载：worker 读取原始文件字节，pump 在主线程解码并安装到 cache_
    // colorSpace 写入 Texture；maxDimension 非 0 时在主线程解码后按比例下采样。
    [[nodiscard]] AssetUID requestLoad(std::string_view resPath, ResourceKind kind = ResourceKind::kTexture,
                                       TextureColorSpace colorSpace = TextureColorSpace::kSrgb,
                                       std::uint32_t maxDimension = 1024);

    [[nodiscard]] LoadState state(AssetUID uid) const;
    [[nodiscard]] std::string error(AssetUID uid) const;

    // maxTextureCompletions：本帧最多安装的纹理 completion 数；0 表示本帧完全不工作
    // maxMeshVertices：本帧 mesh 顶点预算；0 表示本帧不安装 mesh
    void pump(std::size_t maxTextureCompletions = kDefaultTextureCompletionsPerPump,
              std::size_t maxMeshVertices = 65536);

    // 只有 refcount ==1 时删除
    [[nodiscard]] bool evict(AssetUID uid);

    [[nodiscard]] GcStats collectGarbage();
    [[nodiscard]] AssetStats stats() const;

    void clear();

    [[nodiscard]] const std::string& assetRoot() const noexcept;

  private:
    struct Entry {
      std::string sourcePath;
      obj::Ref<Resource> resource;
    };

    void pumpMeshes(std::size_t maxMeshVertices);
    void cancelPendingMesh(AssetUID uid);

    std::string assetRoot_;
    std::unique_ptr<IFileAccess> fileAccess_;
    std::unique_ptr<yr::job::JobSystem> ownedJobs_;
    yr::job::JobSystem* jobs_ = nullptr;
    std::unique_ptr<JobLoadBackend> loadBackend_;
    std::unique_ptr<AsyncLoader> asyncLoader_;
    std::unordered_map<AssetUID, Entry> cache_;
    // job 只持有 uid + source，不持有 obj::Ref<Mesh>，避免阻止 evict/GC。
    std::deque<GltfDeferredMesh> pendingMeshes_;
    std::unordered_set<AssetUID> pendingMeshUids_;
    AssetStats counters_{};
  };

} // namespace yr::asset