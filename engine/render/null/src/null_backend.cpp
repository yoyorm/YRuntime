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
    const std::uint64_t itemCount = snapshot.items.size();
    stats_.items = itemCount;
    stats_.draw_calls = itemCount;
    stats_.triangles = itemCount;
    stats_.snapshot_bytes = encodeSnapshot(snapshot).size();
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