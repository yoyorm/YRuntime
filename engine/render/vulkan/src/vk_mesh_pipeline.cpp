#include "vk_mesh_pipeline.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <yr/core/log.h>

#include "vk_device.h"

namespace yr::render::vk {

  namespace {

    [[nodiscard]] std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path) {
      std::ifstream file(path, std::ios::binary | std::ios::ate);
      if (!file) {
        throw std::runtime_error("无法打开 shader：" + path.string());
      }
      const std::streamsize size = file.tellg();
      if (size <= 0 || size % 4 != 0) {
        throw std::runtime_error("shader 大小非法：" + path.string());
      }
      std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
      file.seekg(0);
      if (!file.read(reinterpret_cast<char*>(words.data()), size)) {
        throw std::runtime_error("读取 shader 失败：" + path.string());
      }
      return words;
    }

    [[nodiscard]] ::vk::raii::ShaderModule createShaderModule(const ::vk::raii::Device& device,
                                                              const std::vector<std::uint32_t>& words) {
      ::vk::ShaderModuleCreateInfo createInfo{};
      createInfo.setCodeSize(words.size() * sizeof(std::uint32_t)).setPCode(words.data());
      return ::vk::raii::ShaderModule(device, createInfo);
    }

  } // namespace

  MeshPipeline::MeshPipeline(const Device& device, ::vk::Format colorFormat, ::vk::Format depthFormat)
      : device_(device.raiiDevice()), colorFormat_(colorFormat), depthFormat_(depthFormat) {
    const std::filesystem::path shaderDir{YR_VULKAN_SHADER_DIR};
    const std::vector<std::uint32_t> vertexWords = readSpirv(shaderDir / "mesh.vert.spv");
    const std::vector<std::uint32_t> fragmentWords = readSpirv(shaderDir / "mesh.frag.spv");

    const ::vk::raii::Device& raiiDevice = device.raiiDevice();
    vertexShader_ = createShaderModule(raiiDevice, vertexWords);
    fragmentShader_ = createShaderModule(raiiDevice, fragmentWords);

    const ::vk::DescriptorSetLayoutBinding frameBinding{0, ::vk::DescriptorType::eUniformBuffer, 1,
                                                        ::vk::ShaderStageFlagBits::eVertex |
                                                            ::vk::ShaderStageFlagBits::eFragment};
    ::vk::DescriptorSetLayoutCreateInfo descriptorLayoutInfo{};
    descriptorLayoutInfo.setBindings(frameBinding);
    descriptorSetLayout_ = ::vk::raii::DescriptorSetLayout(raiiDevice, descriptorLayoutInfo);

    const std::array<::vk::DescriptorSetLayoutBinding, 4> textureBindings{{
        {0, ::vk::DescriptorType::eCombinedImageSampler, 1, ::vk::ShaderStageFlagBits::eFragment},
        {1, ::vk::DescriptorType::eCombinedImageSampler, 1, ::vk::ShaderStageFlagBits::eFragment},
        {2, ::vk::DescriptorType::eCombinedImageSampler, 1, ::vk::ShaderStageFlagBits::eFragment},
        {3, ::vk::DescriptorType::eCombinedImageSampler, 1, ::vk::ShaderStageFlagBits::eFragment},
    }};
    descriptorLayoutInfo.setBindings(textureBindings);
    textureDescriptorSetLayout_ = ::vk::raii::DescriptorSetLayout(raiiDevice, descriptorLayoutInfo);

    const ::vk::PushConstantRange pushConstantRange{::vk::ShaderStageFlagBits::eVertex |
                                                        ::vk::ShaderStageFlagBits::eFragment,
                                                    0, static_cast<std::uint32_t>(sizeof(MeshPushConstants))};
    ::vk::PipelineLayoutCreateInfo layoutInfo{};
    const std::array<::vk::DescriptorSetLayout, 2> setLayouts{*descriptorSetLayout_, *textureDescriptorSetLayout_};
    layoutInfo.setSetLayouts(setLayouts).setPushConstantRanges(pushConstantRange);
    layout_ = ::vk::raii::PipelineLayout(raiiDevice, layoutInfo);

    pipeline_ = createGraphicsPipeline(colorFormat, depthFormat);

    YR_LOG_INFO("render.vk.pipeline", "mesh pipeline 已创建 colorFormat=" + ::vk::to_string(colorFormat) +
                                          " depthFormat=" + ::vk::to_string(depthFormat));
  }

  void MeshPipeline::recreate(::vk::Format colorFormat, ::vk::Format depthFormat) {
    ::vk::raii::Pipeline replacement = createGraphicsPipeline(colorFormat, depthFormat);
    pipeline_ = std::move(replacement);
    colorFormat_ = colorFormat;
    depthFormat_ = depthFormat;
    YR_LOG_INFO("render.vk.pipeline", "mesh pipeline 已重建 colorFormat=" + ::vk::to_string(colorFormat) +
                                          " depthFormat=" + ::vk::to_string(depthFormat));
  }

  ::vk::raii::Pipeline MeshPipeline::createGraphicsPipeline(::vk::Format colorFormat, ::vk::Format depthFormat) const {
    ::vk::PipelineShaderStageCreateInfo vertexStage{};
    vertexStage.setStage(::vk::ShaderStageFlagBits::eVertex).setModule(*vertexShader_).setPName("main");
    ::vk::PipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.setStage(::vk::ShaderStageFlagBits::eFragment).setModule(*fragmentShader_).setPName("main");
    const std::array<::vk::PipelineShaderStageCreateInfo, 2> stages{vertexStage, fragmentStage};

    const ::vk::VertexInputBindingDescription vertexBinding{0, 32, ::vk::VertexInputRate::eVertex};
    const std::array<::vk::VertexInputAttributeDescription, 3> attributes{{
        {0, 0, ::vk::Format::eR32G32B32Sfloat, 0},
        {1, 0, ::vk::Format::eR32G32B32Sfloat, 12},
        {2, 0, ::vk::Format::eR32G32Sfloat, 24},
    }};
    ::vk::PipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.setVertexBindingDescriptions(vertexBinding).setVertexAttributeDescriptions(attributes);

    ::vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.setTopology(::vk::PrimitiveTopology::eTriangleList);

    ::vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.setViewportCount(1).setScissorCount(1);

    ::vk::PipelineRasterizationStateCreateInfo rasterization{};
    rasterization.setPolygonMode(::vk::PolygonMode::eFill)
        .setCullMode(::vk::CullModeFlagBits::eNone)
        .setFrontFace(::vk::FrontFace::eCounterClockwise)
        .setLineWidth(1.0F);

    ::vk::PipelineMultisampleStateCreateInfo multisample{};
    multisample.setRasterizationSamples(::vk::SampleCountFlagBits::e1);

    ::vk::PipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.setBlendEnable(false).setColorWriteMask(
        ::vk::ColorComponentFlagBits::eR | ::vk::ColorComponentFlagBits::eG | ::vk::ColorComponentFlagBits::eB |
        ::vk::ColorComponentFlagBits::eA);
    ::vk::PipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.setAttachmentCount(1).setAttachments(blendAttachment);

    ::vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.setDepthTestEnable(true).setDepthWriteEnable(true).setDepthCompareOp(::vk::CompareOp::eLess);

    constexpr std::array<::vk::DynamicState, 2> dynamicStates{::vk::DynamicState::eViewport,
                                                              ::vk::DynamicState::eScissor};
    ::vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.setDynamicStates(dynamicStates);

    ::vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.setColorAttachmentCount(1)
        .setPColorAttachmentFormats(&colorFormat)
        .setDepthAttachmentFormat(depthFormat);

    ::vk::GraphicsPipelineCreateInfo createInfo{};
    createInfo.setStages(stages)
        .setPVertexInputState(&vertexInput)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterization)
        .setPMultisampleState(&multisample)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlend)
        .setPDynamicState(&dynamicState)
        .setLayout(*layout_)
        .setRenderPass(nullptr)
        .setPNext(&renderingInfo);
    return device_.createGraphicsPipeline(nullptr, createInfo);
  }

} // namespace yr::render::vk
