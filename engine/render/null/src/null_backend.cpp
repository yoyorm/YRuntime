#include <yr/render/null_backend.h>

#include <yr/render/snapshot_io.h>

namespace yr::render {

  bool NullBackend::initialize(const BackendConfig& /*config*/) {
    initialized_ = true;
    return true;
  }

  void NullBackend::shutdown() {
    initialized_ = false;
  }

  void NullBackend::present(const RenderSnapshot& snapshot) {
    stats_ = {};
    const std::uint64_t itemCount = snapshot.items.size();
    stats_.items = itemCount;
    stats_.draw_calls = itemCount;
    stats_.triangles = itemCount;
    stats_.snapshot_bytes = snapshotByteSize(snapshot);
    stats_.uploaded_assets =
        snapshot.uploads.meshes.size() + snapshot.uploads.textures.size() + snapshot.uploads.materials.size();
    stats_.released_assets =
        snapshot.releases.meshes.size() + snapshot.releases.materials.size() + snapshot.releases.textures.size();
    stats_.missing_assets = 0;
    for (const MeshUpload& mesh : snapshot.uploads.meshes) {
      stats_.upload_bytes += mesh.vertices.size() * 32 + mesh.indices.size() * 4;
    }
    for (const TextureUpload& texture : snapshot.uploads.textures) {
      stats_.upload_bytes += texture.rgba8.size();
    }
  }

  const char* NullBackend::name() const noexcept {
    return "null";
  }

  BackendStats NullBackend::last_frame_stats() const noexcept {
    return stats_;
  }

  std::unique_ptr<RendererBackend> create_null_backend() {
    return std::make_unique<NullBackend>();
  }

} // namespace yr::render
