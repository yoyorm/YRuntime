#pragma once

#include <array>

#include "vk_hpp.h"

namespace yr::render::vk {

  class Device;

  struct MeshPushConstants {
    std::array<float, 16> model;
    std::array<float, 4> baseColorFactor;
    std::array<float, 4> materialParams;
  };
  static_assert(sizeof(MeshPushConstants) == 96);

  class MeshPipeline {
  public:
    MeshPipeline(const Device& device, ::vk::Format colorFormat, ::vk::Format depthFormat);
    ~MeshPipeline() = default;
    MeshPipeline(const MeshPipeline&) = delete;
    MeshPipeline& operator=(const MeshPipeline&) = delete;
    MeshPipeline(MeshPipeline&&) = delete;
    MeshPipeline& operator=(MeshPipeline&&) = delete;

    void recreate(::vk::Format colorFormat, ::vk::Format depthFormat);

    [[nodiscard]] ::vk::Pipeline handle() const noexcept { return *pipeline_; }
    [[nodiscard]] ::vk::PipelineLayout layout() const noexcept { return *layout_; }
    [[nodiscard]] ::vk::DescriptorSetLayout descriptorSetLayout() const noexcept { return *descriptorSetLayout_; }
    [[nodiscard]] ::vk::DescriptorSetLayout textureDescriptorSetLayout() const noexcept {
      return *textureDescriptorSetLayout_;
    }
    [[nodiscard]] ::vk::Format colorFormat() const noexcept { return colorFormat_; }
    [[nodiscard]] ::vk::Format depthFormat() const noexcept { return depthFormat_; }

  private:
    [[nodiscard]] ::vk::raii::Pipeline createGraphicsPipeline(::vk::Format colorFormat, ::vk::Format depthFormat) const;

    const ::vk::raii::Device& device_;
    ::vk::raii::ShaderModule vertexShader_{nullptr};
    ::vk::raii::ShaderModule fragmentShader_{nullptr};
    ::vk::raii::DescriptorSetLayout descriptorSetLayout_{nullptr};
    ::vk::raii::DescriptorSetLayout textureDescriptorSetLayout_{nullptr};
    ::vk::raii::PipelineLayout layout_{nullptr};
    ::vk::raii::Pipeline pipeline_{nullptr};
    ::vk::Format colorFormat_{};
    ::vk::Format depthFormat_{};
  };

} // namespace yr::render::vk
