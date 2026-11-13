#pragma once

#include <array>

#include "vk_hpp.h"

namespace yr::render::vk {

  class Device;

  struct ShadowPushConstants {
    std::array<float, 16> model;
    std::array<float, 16> lightViewProj;
  };
  static_assert(sizeof(ShadowPushConstants) == 128);

  // depth-only 方向光阴影 pipeline：无 descriptor set，只有 128B push constant。
  class ShadowPipeline {
  public:
    ShadowPipeline(const Device& device, ::vk::Format depthFormat);
    ~ShadowPipeline() = default;
    ShadowPipeline(const ShadowPipeline&) = delete;
    ShadowPipeline& operator=(const ShadowPipeline&) = delete;
    ShadowPipeline(ShadowPipeline&&) = delete;
    ShadowPipeline& operator=(ShadowPipeline&&) = delete;

    [[nodiscard]] ::vk::Pipeline handle() const noexcept { return *pipeline_; }
    [[nodiscard]] ::vk::PipelineLayout layout() const noexcept { return *layout_; }

  private:
    const ::vk::raii::Device& device_;
    ::vk::raii::ShaderModule vertexShader_{nullptr};
    ::vk::raii::ShaderModule fragmentShader_{nullptr};
    ::vk::raii::PipelineLayout layout_{nullptr};
    ::vk::raii::Pipeline pipeline_{nullptr};
    ::vk::Format depthFormat_{};
  };

} // namespace yr::render::vk
