#pragma once

#include <memory>

#include <yr/render/backend.h>

namespace yr::render {

  // 只统计不画。draw_calls = items 数；triangles = items 数（每 item 一个内建三角形）；
  // snapshot_bytes = encodeSnapshot 的字节数。
  class NullBackend final : public RendererBackend {
  public:
    bool initialize(const BackendConfig& config) override;
    void shutdown() override;
    void present(const RenderSnapshot& snapshot) override;

    [[nodiscard]] const char* name() const noexcept override;
    [[nodiscard]] BackendStats last_frame_stats() const noexcept override;

  private:
    bool initialized_ = false;
    BackendStats stats_{};
  };

  [[nodiscard]] std::unique_ptr<RendererBackend> create_null_backend();

} // namespace yr::render