#include "vk_shadow_pipeline.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
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

  ShadowPipeline::ShadowPipeline(const Device& device, ::vk::Format depthFormat)
      : device_(device.raiiDevice()), depthFormat_(depthFormat) {
    const std::filesystem::path shaderDir{YR_VULKAN_SHADER_DIR};
    const std::vector<std::uint32_t> vertexWords = readSpirv(shaderDir / "shadow.vert.spv");
    const std::vector<std::uint32_t> fragmentWords = readSpirv(shaderDir / "shadow.frag.spv");

    const ::vk::raii::Device& raiiDevice = device.raiiDevice();
    vertexShader_ = createShaderModule(raiiDevice, vertexWords);
    fragmentShader_ = createShaderModule(raiiDevice, fragmentWords);

    const ::vk::PushConstantRange pushConstantRange{::vk::ShaderStageFlagBits::eVertex, 0,
                                                    static_cast<std::uint32_t>(sizeof(ShadowPushConstants))};
    ::vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setPushConstantRanges(pushConstantRange);
    layout_ = ::vk::raii::PipelineLayout(raiiDevice, layoutInfo);

    ::vk::PipelineShaderStageCreateInfo vertexStage{};
    vertexStage.setStage(::vk::ShaderStageFlagBits::eVertex).setModule(*vertexShader_).setPName("main");
    ::vk::PipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.setStage(::vk::ShaderStageFlagBits::eFragment).setModule(*fragmentShader_).setPName("main");
    const std::array<::vk::PipelineShaderStageCreateInfo, 2> stages{vertexStage, fragmentStage};

    const ::vk::VertexInputBindingDescription vertexBinding{0, 32, ::vk::VertexInputRate::eVertex};
    const std::array<::vk::VertexInputAttributeDescription, 1> attributes{{
        {0, 0, ::vk::Format::eR32G32B32Sfloat, 0},
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

    // depth-only：无 color attachment。
    ::vk::PipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.setAttachmentCount(0);

    ::vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.setDepthTestEnable(true).setDepthWriteEnable(true).setDepthCompareOp(::vk::CompareOp::eLess);

    constexpr std::array<::vk::DynamicState, 2> dynamicStates{::vk::DynamicState::eViewport,
                                                              ::vk::DynamicState::eScissor};
    ::vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.setDynamicStates(dynamicStates);

    ::vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.setColorAttachmentCount(0).setDepthAttachmentFormat(depthFormat);

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
    pipeline_ = device_.createGraphicsPipeline(nullptr, createInfo);

    YR_LOG_INFO("render.vk.pipeline", "shadow pipeline 已创建 depthFormat=" + ::vk::to_string(depthFormat));
  }

} // namespace yr::render::vk
