#pragma once

#include <array>

#include "vk_hpp.h"

namespace yr::render::vk {

  class Device;

  // 与 triangle.vert / triangle.frag 的 push_constant block 同布局：
  // mat4 mvp（column-major）+ vec4 color，std430 下 offset 0 / 64。
  struct PushConstants {
    std::array<float, 16> mvp;
    std::array<float, 4> color;
  };
  static_assert(sizeof(PushConstants) == 80);

  // Graphics pipeline 的所有者；依赖 device + color format，必须先于 device 销毁。
  // 顶点由 vertex shader 的 gl_VertexIndex 生成（无 vertex buffer）；viewport/scissor 是 dynamic state。
  class TrianglePipeline {
  public:
    // colorFormat 必须来自当前 swapchain；失败抛异常。
    TrianglePipeline(const Device& device, ::vk::Format colorFormat);
    ~TrianglePipeline() = default;
    TrianglePipeline(const TrianglePipeline&) = delete;
    TrianglePipeline& operator=(const TrianglePipeline&) = delete;
    TrianglePipeline(TrianglePipeline&&) = delete;
    TrianglePipeline& operator=(TrianglePipeline&&) = delete;

    [[nodiscard]] ::vk::Pipeline handle() const noexcept { return *pipeline_; }
    [[nodiscard]] ::vk::PipelineLayout layout() const noexcept { return *layout_; }
    [[nodiscard]] ::vk::Format colorFormat() const noexcept { return colorFormat_; }

  private:
    ::vk::raii::ShaderModule vertexShader_{nullptr};
    ::vk::raii::ShaderModule fragmentShader_{nullptr};
    ::vk::raii::PipelineLayout layout_{nullptr};
    ::vk::raii::Pipeline pipeline_{nullptr};
    ::vk::Format colorFormat_{};
  };

} // namespace yr::render::vk