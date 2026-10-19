#pragma once

#include <cstdint>

#include <yr/render/snapshot.h>

namespace yr::render {

  // M5 headless：无窗口/设备参数；字段等到 M6 有消费者再加。
  struct BackendConfig {};

  struct BackendStats {
    std::uint64_t items = 0;
    std::uint64_t draw_calls = 0;
    std::uint64_t triangles = 0;
    std::uint64_t snapshot_bytes = 0;
  };

  // 消费快照的后端接口
  class RendererBackend {
  public:
    virtual ~RendererBackend() = default;

    virtual bool initialize(const BackendConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual void present(const RenderSnapshot& snapshot) = 0;

    [[nodiscard]] virtual const char* name() const noexcept = 0;
    [[nodiscard]] virtual BackendStats last_frame_stats() const noexcept = 0;
  };

} // namespace yr::render