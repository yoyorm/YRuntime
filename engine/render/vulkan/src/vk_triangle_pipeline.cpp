#include "vk_triangle_pipeline.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <yr/core/log.h>

#include "vk_device.h"

namespace yr::render::vk {

  namespace {

    // SPIR-V 是 32 位字序列：vector<uint32_t> 保证 4 字节对齐，codeSize 必须是 4 的倍数。
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

  TrianglePipeline::TrianglePipeline(const Device& device, ::vk::Format colorFormat) : colorFormat_(colorFormat) {
    const std::filesystem::path shaderDir{YR_VULKAN_SHADER_DIR};
    const std::vector<std::uint32_t> vertexWords = readSpirv(shaderDir / "triangle.vert.spv");
    const std::vector<std::uint32_t> fragmentWords = readSpirv(shaderDir / "triangle.frag.spv");

    const ::vk::raii::Device& raiiDevice = device.raiiDevice();
    vertexShader_ = createShaderModule(raiiDevice, vertexWords);
    fragmentShader_ = createShaderModule(raiiDevice, fragmentWords);

    // 空 descriptor layout + 80 字节 push constant：MVP 与颜色逐 draw 推送。
    const ::vk::PushConstantRange pushConstantRange{::vk::ShaderStageFlagBits::eVertex |
                                                        ::vk::ShaderStageFlagBits::eFragment,
                                                    0, static_cast<std::uint32_t>(sizeof(PushConstants))};
    ::vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setPushConstantRanges(pushConstantRange);
    layout_ = ::vk::raii::PipelineLayout(raiiDevice, layoutInfo);

    ::vk::PipelineShaderStageCreateInfo vertexStage{};
    vertexStage.setStage(::vk::ShaderStageFlagBits::eVertex).setModule(*vertexShader_).setPName("main");
    ::vk::PipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.setStage(::vk::ShaderStageFlagBits::eFragment).setModule(*fragmentShader_).setPName("main");
    const std::array<::vk::PipelineShaderStageCreateInfo, 2> stages{vertexStage, fragmentStage};

    // 无 vertex buffer：顶点位置完全由 vertex shader 的 gl_VertexIndex 生成。
    const ::vk::PipelineVertexInputStateCreateInfo vertexInput{};

    ::vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.setTopology(::vk::PrimitiveTopology::eTriangleList);

    // viewport / scissor 走 dynamic state，数量仍须声明；具体值在录制命令时设置。
    ::vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.setViewportCount(1).setScissorCount(1);

    ::vk::PipelineRasterizationStateCreateInfo rasterization{};
    rasterization.setPolygonMode(::vk::PolygonMode::eFill)
        .setCullMode(::vk::CullModeFlagBits::eNone)
        .setFrontFace(::vk::FrontFace::eCounterClockwise)
        .setLineWidth(1.0f);

    ::vk::PipelineMultisampleStateCreateInfo multisample{};
    multisample.setRasterizationSamples(::vk::SampleCountFlagBits::e1);

    ::vk::PipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.setBlendEnable(false).setColorWriteMask(
        ::vk::ColorComponentFlagBits::eR | ::vk::ColorComponentFlagBits::eG | ::vk::ColorComponentFlagBits::eB |
        ::vk::ColorComponentFlagBits::eA);
    ::vk::PipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.setAttachmentCount(1).setAttachments(blendAttachment);

    const ::vk::PipelineDepthStencilStateCreateInfo depthStencil{};

    constexpr std::array<::vk::DynamicState, 2> kDynamicStates{::vk::DynamicState::eViewport,
                                                               ::vk::DynamicState::eScissor};
    ::vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.setDynamicStates(kDynamicStates);

    // dynamic rendering：没有 render pass，color format 必须在这里显式给出。
    ::vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.setColorAttachmentCount(1).setPColorAttachmentFormats(&colorFormat_);

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
    pipeline_ = raiiDevice.createGraphicsPipeline(nullptr, createInfo);

    YR_LOG_INFO("render.vk.pipeline",
                "pipeline 已创建 colorFormat=" + ::vk::to_string(colorFormat) + " dynamic viewport/scissor");
  }

} // namespace yr::render::vk