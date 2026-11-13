#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <yr/asset/file_access.h>
#include <yr/asset/gltf_loader.h>
#include <yr/asset/resource.h>
#include <yr/object/ref.h>

namespace yr::asset {

  class LoaderWorker;
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
    // 只统计尚未安装的 deferred mesh job，不含 job 持有的 model source 内存。
    std::size_t pending_meshes = 0;
  };

  struct GcStats {
    std::size_t evicted = 0;
    std::size_t passes = 0;
  };

  // 已加载资源的 UID -> 强引用缓存，负责驻留统计与 fixpoint GC。
  // 仅允许主线程使用：ObjectDB、RefCounted 的释放路径和本类都没有跨线程同步。
  // 异步读取只发生在内部 LoaderWorker 线程；解码与对象构造仍在调用 pump 的主线程。
  class AssetDatabase final {
  public:
    explicit AssetDatabase(std::string assetRoot);
    // access 非空且归本类所有；用于 Memory/Throttled 等注入实现。
    explicit AssetDatabase(std::unique_ptr<IFileAccess> access);
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

    // 解析 glTF 结构并通过 hooks 复用/注册资源；失败时回滚本次新增的资源。
    // deferTextures：Texture 只注册 kLoading 占位，并通过 AsyncLoader 排队异步加载。
    // deferMeshes：Mesh 只注册 kLoading 占位，job 由本类接管，后续 pumpMeshes 安装。
    // skipTextures：不创建 Texture，也不产生 pendingTextures。
    [[nodiscard]] GltfLoadResult loadGltfScene(std::string_view resPath, const GltfLoadOptions& options = {});

    // 异步加载：worker 读取原始文件字节，pump 在主线程解码并安装到 cache_。
    // cache 中已 ready 时直接返回，不再重复 submit。
    [[nodiscard]] AssetUID requestLoad(std::string_view resPath, ResourceKind kind = ResourceKind::kTexture);
    // 优先查询 cache_，异步结果尚未安装时才回退到 AsyncLoader。
    [[nodiscard]] LoadState state(AssetUID uid) const;
    [[nodiscard]] std::string error(AssetUID uid) const;

    // maxTextureCompletions：本帧最多安装的纹理 completion 数；0 表示本帧完全不工作。
    // maxMeshVertices：本帧 mesh 顶点预算；0 表示本帧不安装 mesh。至少安装 1 个避免饥饿。
    void pump(std::size_t maxTextureCompletions = 1, std::size_t maxMeshVertices = 65536);

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

    void pumpMeshes(std::size_t maxMeshVertices);
    void cancelPendingMesh(AssetUID uid);

    std::string assetRoot_;
    // 声明顺序即析构顺序的逆序：async → worker（join）→ access。
    std::unique_ptr<IFileAccess> fileAccess_;
    std::unique_ptr<LoaderWorker> loaderWorker_;
    std::unique_ptr<AsyncLoader> asyncLoader_;
    std::unordered_map<AssetUID, Entry> cache_;
    // job 只持有 uid + source，不持有 obj::Ref<Mesh>，避免阻止 evict/GC。
    std::deque<GltfDeferredMesh> pendingMeshes_;
    std::unordered_set<AssetUID> pendingMeshUids_;
    AssetStats counters_{}; // 只使用 registered/evicted/loads/hits/misses/failed
  };

} // namespace yr::asset