#include <yr/render/vk/vulkan_backend.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <yr/core/log.h>
#include <yr/math/mat4.h>
#include <yr/math/vec3.h>
#include <yr/render/snapshot_io.h>

#include "platform_window.h"
#include "vk_command_pool.h"
#include "vk_depth_buffer.h"
#include "vk_device.h"
#include "vk_frame_sync.h"
#include "vk_instance.h"
#include "vk_mesh_pipeline.h"
#include "vk_surface.h"
#include "vk_swapchain.h"

namespace yr::render {

  namespace {

    constexpr std::array<float, 4> kClearColor{0.10F, 0.18F, 0.30F, 1.0F};
    constexpr std::uint32_t kFramesInFlight = 2;
    constexpr ::vk::Format kDepthFormat = ::vk::Format::eD32Sfloat;

    struct FrameUniforms {
      std::array<float, 16> view;
      std::array<float, 16> proj;
      std::array<float, 4> eye;
      std::array<float, 4> lightDir;
      std::array<float, 4> lightColor;
    };
    static_assert(sizeof(FrameUniforms) == 176);
    static_assert(sizeof(TransformPod) == sizeof(math::Mat4));

    [[nodiscard]] math::Mat4 toMat4(const TransformPod& transform) noexcept {
      return std::bit_cast<math::Mat4>(transform);
    }

    [[nodiscard]] std::uint32_t findMemoryType(const ::vk::PhysicalDevice& physicalDevice, std::uint32_t typeBits,
                                               ::vk::MemoryPropertyFlags properties) {
      const ::vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
      for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if ((typeBits & (1U << index)) != 0 &&
            (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties) {
          return index;
        }
      }
      throw std::runtime_error("没有符合要求的 Vulkan memory type");
    }

  } // namespace

  struct VulkanBackend::Impl {
    struct GpuMesh {
      ::vk::raii::DeviceMemory memory{nullptr};
      ::vk::raii::Buffer buffer{nullptr};
      std::uint32_t indexCount = 0;
      std::uint32_t vertexBytes = 0;
    };

    struct GpuMaterial {
      std::array<float, 4> baseColorFactor{};
      float metallic = 0.0F;
      float roughness = 1.0F;
      std::uint64_t baseColorTexture = 0;
    };

    struct GpuTexture {
      ::vk::raii::DeviceMemory memory{nullptr};
      ::vk::raii::Image image{nullptr};
      ::vk::raii::ImageView view{nullptr};
      ::vk::raii::DescriptorSet descriptorSet{nullptr};
    };

    struct FrameBuffer {
      ::vk::raii::DeviceMemory memory{nullptr};
      ::vk::raii::Buffer buffer{nullptr};
      void* mapped = nullptr;
    };

    // 声明顺序遵循依赖：window → instance → surface → device → swapchain/depth → pipeline → command/sync.
    std::optional<vk::Window> window;
    std::optional<vk::Instance> instance;
    std::optional<vk::Surface> surface;
    std::optional<vk::Device> device;
    std::optional<vk::Swapchain> swapchain;
    std::vector<vk::DepthBuffer> depthBuffers;
    std::optional<vk::MeshPipeline> meshPipeline;
    std::optional<vk::CommandPool> commandPool;
    std::optional<vk::FrameSync> frameSync;
    std::optional<vk::CommandPool> uploadCommandPool;
    std::optional<::vk::raii::Sampler> sampler;

    std::array<FrameBuffer, kFramesInFlight> frameBuffers;
    std::optional<::vk::raii::DescriptorPool> descriptorPool;
    std::vector<::vk::raii::DescriptorSet> descriptorSets;

    std::unordered_map<std::uint64_t, GpuMesh> meshes;
    std::unordered_map<std::uint64_t, GpuMaterial> materials;
    std::unordered_map<std::uint64_t, GpuTexture> textures;
    std::array<std::vector<GpuMesh>, kFramesInFlight> pendingMeshes;
    std::array<std::vector<GpuTexture>, kFramesInFlight> pendingTextures;
    std::optional<GpuTexture> defaultTexture;
    std::array<bool, kFramesInFlight> depthInitialized{};

    std::uint32_t frameSlot_ = 0;
    BackendStats stats{};
    std::uint32_t lastValidationErrors = 0;

    void createDepthBuffers() {
      depthBuffers.clear();
      depthBuffers.reserve(kFramesInFlight);
      for (std::uint32_t slot = 0; slot < kFramesInFlight; ++slot) {
        depthBuffers.emplace_back(*device, swapchain->extent(), kDepthFormat);
      }
      depthInitialized.fill(false);
    }

    void createFrameResources() {
      const ::vk::raii::Device& raiiDevice = device->raiiDevice();
      for (FrameBuffer& frame : frameBuffers) {
        ::vk::BufferCreateInfo bufferInfo{};
        bufferInfo.setSize(sizeof(FrameUniforms))
            .setUsage(::vk::BufferUsageFlagBits::eUniformBuffer)
            .setSharingMode(::vk::SharingMode::eExclusive);
        frame.buffer = ::vk::raii::Buffer(raiiDevice, bufferInfo);
        const ::vk::MemoryRequirements requirements = frame.buffer.getMemoryRequirements();
        ::vk::MemoryAllocateInfo allocateInfo{};
        allocateInfo.setAllocationSize(requirements.size)
            .setMemoryTypeIndex(findMemoryType(device->physicalDevice(), requirements.memoryTypeBits,
                                               ::vk::MemoryPropertyFlagBits::eHostVisible |
                                                   ::vk::MemoryPropertyFlagBits::eHostCoherent));
        frame.memory = ::vk::raii::DeviceMemory(raiiDevice, allocateInfo);
        frame.buffer.bindMemory(*frame.memory, 0);
        frame.mapped = frame.memory.mapMemory(0, sizeof(FrameUniforms));
      }

      const std::array<::vk::DescriptorPoolSize, 2> poolSizes{{
          {::vk::DescriptorType::eUniformBuffer, kFramesInFlight},
          // M9.0c texture budget; increase for Sponza if needed.
          {::vk::DescriptorType::eCombinedImageSampler, 256},
      }};
      ::vk::DescriptorPoolCreateInfo poolInfo{};
      poolInfo.setFlags(::vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
          .setMaxSets(kFramesInFlight + 256)
          .setPoolSizes(poolSizes);
      descriptorPool.emplace(raiiDevice, poolInfo);

      const std::array<::vk::DescriptorSetLayout, kFramesInFlight> layouts{meshPipeline->descriptorSetLayout(),
                                                                           meshPipeline->descriptorSetLayout()};
      ::vk::DescriptorSetAllocateInfo allocateInfo{};
      allocateInfo.setDescriptorPool(**descriptorPool).setSetLayouts(layouts);
      descriptorSets = raiiDevice.allocateDescriptorSets(allocateInfo);

      for (std::uint32_t slot = 0; slot < kFramesInFlight; ++slot) {
        const ::vk::DescriptorBufferInfo bufferInfo{*frameBuffers[slot].buffer, 0, sizeof(FrameUniforms)};
        ::vk::WriteDescriptorSet write{};
        write.setDstSet(*descriptorSets[slot])
            .setDstBinding(0)
            .setDescriptorType(::vk::DescriptorType::eUniformBuffer)
            .setBufferInfo(bufferInfo);
        const std::array<::vk::WriteDescriptorSet, 1> writes{write};
        raiiDevice.updateDescriptorSets(writes, {});
      }
    }

    [[nodiscard]] bool validTextureData(const TextureUpload& upload) const {
      if (upload.width == 0 || upload.height == 0 ||
          upload.width > device->physicalDevice().getProperties().limits.maxImageDimension2D ||
          upload.height > device->physicalDevice().getProperties().limits.maxImageDimension2D ||
          upload.width > std::numeric_limits<std::size_t>::max() / upload.height / 4) {
        YR_LOG_WARN("render.vk.texture", "忽略尺寸非法的 baseColor 纹理");
        return false;
      }
      const std::size_t byteCount = static_cast<std::size_t>(upload.width) * upload.height * 4;
      if (upload.rgba8.size() != byteCount) {
        YR_LOG_WARN("render.vk.texture", "忽略字节数非法的 baseColor 纹理");
        return false;
      }
      return true;
    }

    [[nodiscard]] std::vector<GpuTexture> createGpuTextures(const std::vector<TextureUpload>& uploads) {
      struct StagingBuffer {
        ::vk::raii::DeviceMemory memory{nullptr};
        ::vk::raii::Buffer buffer{nullptr};
      };

      if (uploads.empty()) {
        return {};
      }
      const ::vk::raii::Device& raiiDevice = device->raiiDevice();
      std::vector<StagingBuffer> stagingBuffers;
      std::vector<GpuTexture> gpuTextures;
      stagingBuffers.reserve(uploads.size());
      gpuTextures.reserve(uploads.size());
      for (const TextureUpload& upload : uploads) {
        stagingBuffers.emplace_back();
        StagingBuffer& staging = stagingBuffers.back();
        ::vk::BufferCreateInfo bufferInfo{};
        bufferInfo.setSize(upload.rgba8.size())
            .setUsage(::vk::BufferUsageFlagBits::eTransferSrc)
            .setSharingMode(::vk::SharingMode::eExclusive);
        staging.buffer = ::vk::raii::Buffer(raiiDevice, bufferInfo);
        const ::vk::MemoryRequirements bufferRequirements = staging.buffer.getMemoryRequirements();
        ::vk::MemoryAllocateInfo bufferAllocateInfo{};
        bufferAllocateInfo.setAllocationSize(bufferRequirements.size)
            .setMemoryTypeIndex(findMemoryType(device->physicalDevice(), bufferRequirements.memoryTypeBits,
                                               ::vk::MemoryPropertyFlagBits::eHostVisible |
                                                   ::vk::MemoryPropertyFlagBits::eHostCoherent));
        staging.memory = ::vk::raii::DeviceMemory(raiiDevice, bufferAllocateInfo);
        staging.buffer.bindMemory(*staging.memory, 0);
        void* mapped = staging.memory.mapMemory(0, upload.rgba8.size());
        std::memcpy(mapped, upload.rgba8.data(), upload.rgba8.size());
        staging.memory.unmapMemory();

        gpuTextures.emplace_back();
        GpuTexture& texture = gpuTextures.back();
        ::vk::ImageCreateInfo imageInfo{};
        imageInfo.setImageType(::vk::ImageType::e2D)
            .setFormat(::vk::Format::eR8G8B8A8Srgb)
            .setExtent(::vk::Extent3D{upload.width, upload.height, 1})
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(::vk::SampleCountFlagBits::e1)
            .setTiling(::vk::ImageTiling::eOptimal)
            .setUsage(::vk::ImageUsageFlagBits::eTransferDst | ::vk::ImageUsageFlagBits::eSampled)
            .setSharingMode(::vk::SharingMode::eExclusive)
            .setInitialLayout(::vk::ImageLayout::eUndefined);
        texture.image = ::vk::raii::Image(raiiDevice, imageInfo);
        const ::vk::MemoryRequirements imageRequirements = texture.image.getMemoryRequirements();
        ::vk::MemoryAllocateInfo imageAllocateInfo{};
        imageAllocateInfo.setAllocationSize(imageRequirements.size)
            .setMemoryTypeIndex(findMemoryType(device->physicalDevice(), imageRequirements.memoryTypeBits,
                                               ::vk::MemoryPropertyFlagBits::eDeviceLocal));
        texture.memory = ::vk::raii::DeviceMemory(raiiDevice, imageAllocateInfo);
        texture.image.bindMemory(*texture.memory, 0);

        ::vk::ImageViewCreateInfo viewInfo{};
        viewInfo.setImage(*texture.image)
            .setViewType(::vk::ImageViewType::e2D)
            .setFormat(::vk::Format::eR8G8B8A8Srgb)
            .setSubresourceRange(::vk::ImageSubresourceRange{::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        texture.view = ::vk::raii::ImageView(raiiDevice, viewInfo);
      }

      const ::vk::CommandBuffer cmd = uploadCommandPool->commandBuffer(0);
      cmd.reset();
      ::vk::CommandBufferBeginInfo beginInfo{};
      beginInfo.setFlags(::vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
      cmd.begin(beginInfo);
      for (std::size_t index = 0; index < uploads.size(); ++index) {
        GpuTexture& texture = gpuTextures[index];
        const TextureUpload& upload = uploads[index];
        const ::vk::ImageSubresourceRange range{::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        ::vk::ImageMemoryBarrier2 toTransfer{};
        toTransfer.setSrcStageMask(::vk::PipelineStageFlagBits2::eNone)
            .setSrcAccessMask(::vk::AccessFlagBits2::eNone)
            .setDstStageMask(::vk::PipelineStageFlagBits2::eTransfer)
            .setDstAccessMask(::vk::AccessFlagBits2::eTransferWrite)
            .setOldLayout(::vk::ImageLayout::eUndefined)
            .setNewLayout(::vk::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(::vk::QueueFamilyIgnored)
            .setDstQueueFamilyIndex(::vk::QueueFamilyIgnored)
            .setImage(*texture.image)
            .setSubresourceRange(range);
        ::vk::DependencyInfo dependency{};
        dependency.setImageMemoryBarriers(toTransfer);
        cmd.pipelineBarrier2(dependency);

        const ::vk::BufferImageCopy copyRegion{0,
                                               0,
                                               0,
                                               ::vk::ImageSubresourceLayers{::vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                               ::vk::Offset3D{0, 0, 0},
                                               ::vk::Extent3D{upload.width, upload.height, 1}};
        cmd.copyBufferToImage(*stagingBuffers[index].buffer, *texture.image, ::vk::ImageLayout::eTransferDstOptimal,
                              copyRegion);

        ::vk::ImageMemoryBarrier2 toShaderRead{};
        toShaderRead.setSrcStageMask(::vk::PipelineStageFlagBits2::eTransfer)
            .setSrcAccessMask(::vk::AccessFlagBits2::eTransferWrite)
            .setDstStageMask(::vk::PipelineStageFlagBits2::eFragmentShader)
            .setDstAccessMask(::vk::AccessFlagBits2::eShaderSampledRead)
            .setOldLayout(::vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(::vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(::vk::QueueFamilyIgnored)
            .setDstQueueFamilyIndex(::vk::QueueFamilyIgnored)
            .setImage(*texture.image)
            .setSubresourceRange(range);
        dependency.setImageMemoryBarriers(toShaderRead);
        cmd.pipelineBarrier2(dependency);
      }
      cmd.end();
      ::vk::CommandBufferSubmitInfo commandBufferInfo{};
      commandBufferInfo.setCommandBuffer(cmd);
      ::vk::SubmitInfo2 submitInfo{};
      submitInfo.setCommandBufferInfos(commandBufferInfo);
      device->graphicsQueue().submit2(submitInfo, nullptr);
      device->graphicsQueue().waitIdle();

      for (GpuTexture& texture : gpuTextures) {
        const ::vk::DescriptorSetLayout layout = meshPipeline->textureDescriptorSetLayout();
        ::vk::DescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.setDescriptorPool(**descriptorPool).setSetLayouts(layout);
        auto allocatedSets = raiiDevice.allocateDescriptorSets(allocateInfo);
        texture.descriptorSet = std::move(allocatedSets.front());
        const ::vk::DescriptorImageInfo imageInfo{**sampler, *texture.view, ::vk::ImageLayout::eShaderReadOnlyOptimal};
        ::vk::WriteDescriptorSet write{};
        write.setDstSet(*texture.descriptorSet)
            .setDstBinding(0)
            .setDescriptorType(::vk::DescriptorType::eCombinedImageSampler)
            .setImageInfo(imageInfo);
        const std::array<::vk::WriteDescriptorSet, 1> writes{write};
        raiiDevice.updateDescriptorSets(writes, {});
      }
      return gpuTextures;
    }

    void uploadTextures(const std::vector<TextureUpload>& uploads) {
      if (uploads.empty()) {
        return;
      }
      std::vector<TextureUpload> validUploads;
      std::vector<std::uint64_t> handles;
      std::unordered_set<std::uint64_t> seen;
      validUploads.reserve(uploads.size());
      handles.reserve(uploads.size());
      for (const TextureUpload& upload : uploads) {
        if (!upload.texture.isValid() || textures.contains(upload.texture.value) ||
            !seen.insert(upload.texture.value).second) {
          continue;
        }
        if (!validTextureData(upload)) {
          continue;
        }
        validUploads.push_back(upload);
        handles.push_back(upload.texture.value);
      }
      std::vector<GpuTexture> gpuTextures = createGpuTextures(validUploads);
      for (std::size_t index = 0; index < gpuTextures.size(); ++index) {
        const std::size_t byteCount = validUploads[index].rgba8.size();
        textures.emplace(handles[index], std::move(gpuTextures[index]));
        ++stats.uploaded_assets;
        stats.upload_bytes += byteCount;
      }
    }

    void createDefaultTexture() {
      TextureUpload white;
      white.width = 1;
      white.height = 1;
      white.rgba8 = {255, 255, 255, 255};
      std::vector<GpuTexture> uploaded = createGpuTextures({white});
      defaultTexture.emplace(std::move(uploaded.front()));
    }

    void updateFrameUniforms(std::uint32_t slot, const CameraPod& camera) {
      const math::Vec3 light = math::Vec3{0.4F, 0.8F, 0.5F}.normalized();
      const FrameUniforms uniforms{toMat4(camera.view).m_,
                                   toMat4(camera.proj).m_,
                                   {camera.eye[0], camera.eye[1], camera.eye[2], 1.0F},
                                   {light.x, light.y, light.z, 0.0F},
                                   {1.0F, 1.0F, 1.0F, 1.0F}};
      std::memcpy(frameBuffers[slot].mapped, &uniforms, sizeof(uniforms));
    }

    void uploadMesh(const MeshUpload& upload) {
      if (!upload.mesh.isValid() || meshes.contains(upload.mesh.value)) {
        return;
      }
      if (upload.vertices.empty() || upload.indices.empty()) {
        ++stats.missing_assets;
        return;
      }

      const std::size_t vertexBytes = upload.vertices.size() * sizeof(MeshVertexPod);
      const std::size_t indexBytes = upload.indices.size() * sizeof(std::uint32_t);
      const std::size_t totalBytes = vertexBytes + indexBytes;
      if (vertexBytes > std::numeric_limits<std::uint32_t>::max() ||
          upload.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        ++stats.missing_assets;
        return;
      }

      GpuMesh mesh;
      const ::vk::raii::Device& raiiDevice = device->raiiDevice();
      ::vk::BufferCreateInfo bufferInfo{};
      bufferInfo.setSize(totalBytes)
          .setUsage(::vk::BufferUsageFlagBits::eVertexBuffer | ::vk::BufferUsageFlagBits::eIndexBuffer)
          .setSharingMode(::vk::SharingMode::eExclusive);
      mesh.buffer = ::vk::raii::Buffer(raiiDevice, bufferInfo);
      const ::vk::MemoryRequirements requirements = mesh.buffer.getMemoryRequirements();
      ::vk::MemoryAllocateInfo allocateInfo{};
      allocateInfo.setAllocationSize(requirements.size)
          .setMemoryTypeIndex(
              findMemoryType(device->physicalDevice(), requirements.memoryTypeBits,
                             ::vk::MemoryPropertyFlagBits::eHostVisible | ::vk::MemoryPropertyFlagBits::eHostCoherent));
      mesh.memory = ::vk::raii::DeviceMemory(raiiDevice, allocateInfo);
      mesh.buffer.bindMemory(*mesh.memory, 0);
      void* mapped = mesh.memory.mapMemory(0, totalBytes);
      std::memcpy(mapped, upload.vertices.data(), vertexBytes);
      std::memcpy(static_cast<std::byte*>(mapped) + vertexBytes, upload.indices.data(), indexBytes);
      mesh.memory.unmapMemory();
      mesh.indexCount = static_cast<std::uint32_t>(upload.indices.size());
      mesh.vertexBytes = static_cast<std::uint32_t>(vertexBytes);
      meshes.emplace(upload.mesh.value, std::move(mesh));
      ++stats.uploaded_assets;
      stats.upload_bytes += totalBytes;
    }

    void uploadMaterial(const MaterialUpload& upload) {
      if (!upload.material.isValid() || materials.contains(upload.material.value)) {
        return;
      }
      GpuMaterial material;
      std::copy(std::begin(upload.baseColorFactor), std::end(upload.baseColorFactor), material.baseColorFactor.begin());
      material.metallic = upload.metallic;
      material.roughness = upload.roughness;
      material.baseColorTexture = upload.baseColorTexture.value;
      materials.emplace(upload.material.value, material);
      ++stats.uploaded_assets;
    }

    void processResourceChanges(const RenderSnapshot& snapshot) {
      for (AssetHandle handle : snapshot.releases.meshes) {
        auto it = meshes.find(handle.value);
        if (it != meshes.end()) {
          // frameSlot_ is the slot about to be used; the previous frame used the other slot.
          // With two frames in flight, retain the mesh until that previous slot's fence is waited.
          const std::uint32_t pendingSlot = (frameSlot_ + kFramesInFlight - 1) % kFramesInFlight;
          pendingMeshes[pendingSlot].push_back(std::move(it->second));
          meshes.erase(it);
          ++stats.released_assets;
        }
      }
      for (AssetHandle handle : snapshot.releases.materials) {
        if (materials.erase(handle.value) != 0) {
          ++stats.released_assets;
        }
      }
      for (AssetHandle handle : snapshot.releases.textures) {
        auto it = textures.find(handle.value);
        if (it != textures.end()) {
          const std::uint32_t pendingSlot = (frameSlot_ + kFramesInFlight - 1) % kFramesInFlight;
          pendingTextures[pendingSlot].push_back(std::move(it->second));
          textures.erase(it);
          ++stats.released_assets;
        }
      }
      for (const MeshUpload& upload : snapshot.uploads.meshes) {
        uploadMesh(upload);
      }
      uploadTextures(snapshot.uploads.textures);
      for (const MaterialUpload& upload : snapshot.uploads.materials) {
        uploadMaterial(upload);
      }
    }

    void shutdown() {
      if (!instance.has_value()) {
        return;
      }
      if (device.has_value()) {
        device->raiiDevice().waitIdle();
      }
      meshes.clear();
      materials.clear();
      textures.clear();
      for (auto& pending : pendingMeshes) {
        pending.clear();
      }
      for (auto& pending : pendingTextures) {
        pending.clear();
      }
      defaultTexture.reset();
      descriptorSets.clear();
      descriptorPool.reset();
      sampler.reset();
      uploadCommandPool.reset();
      for (FrameBuffer& frame : frameBuffers) {
        if (frame.mapped != nullptr && device.has_value()) {
          frame.memory.unmapMemory();
          frame.mapped = nullptr;
        }
      }
      frameSync.reset();
      commandPool.reset();
      meshPipeline.reset();
      depthBuffers.clear();
      swapchain.reset();
      for (FrameBuffer& frame : frameBuffers) {
        frame.buffer = nullptr;
        frame.memory = nullptr;
      }
      device.reset();
      surface.reset();
      const vk::DebugStats debugStats = instance->debugStats();
      lastValidationErrors = debugStats.errors;
      instance.reset();
      window.reset();
      stats = BackendStats{};
      YR_LOG_INFO("render.vk.backend", "shutdown validation_errors=" + std::to_string(debugStats.errors) +
                                           " validation_warnings=" + std::to_string(debugStats.warnings));
    }

    bool tryRecreate() {
      try {
        device->raiiDevice().waitIdle();
        swapchain->recreate(*device, *surface, *window);
        depthBuffers.clear();
        createDepthBuffers();
        if (meshPipeline.has_value()) {
          meshPipeline->recreate(swapchain->format(), kDepthFormat);
        } else {
          meshPipeline.emplace(*device, swapchain->format(), kDepthFormat);
        }
        frameSync.reset();
        frameSync.emplace(*device, kFramesInFlight, swapchain->imageCount());
        frameSlot_ = 0;
        for (auto& pending : pendingMeshes) {
          pending.clear();
        }
        for (auto& pending : pendingTextures) {
          pending.clear();
        }
        YR_LOG_INFO("render.vk.backend", "swapchain 已重建 " + std::to_string(swapchain->extent().width) + "x" +
                                             std::to_string(swapchain->extent().height) +
                                             " imageCount=" + std::to_string(swapchain->imageCount()));
        return true;
      } catch (const std::exception& error) {
        YR_LOG_ERROR("render.vk.backend", std::string("swapchain 重建失败：") + error.what());
        return false;
      }
    }

    void renderFrame(const RenderSnapshot& snapshot) {
      if (!window.has_value() || !surface.has_value() || !device.has_value() || !swapchain.has_value() ||
          !meshPipeline.has_value() || !commandPool.has_value() || !frameSync.has_value()) {
        return;
      }

      const std::uint32_t slot = frameSlot_;
      const ::vk::raii::Device& raiiDevice = device->raiiDevice();
      const ::vk::Result waitResult =
          raiiDevice.waitForFences(frameSync->inFlight(slot), ::vk::True, std::numeric_limits<std::uint64_t>::max());
      if (waitResult != ::vk::Result::eSuccess) {
        YR_LOG_ERROR("render.vk.backend", "waitForFences 失败：" + ::vk::to_string(waitResult));
        return;
      }
      // This slot's fence has completed, so resources deferred to it are no longer in use.
      pendingMeshes[slot].clear();
      pendingTextures[slot].clear();
      updateFrameUniforms(slot, snapshot.camera);

      const ::vk::ResultValue<std::uint32_t> acquired = swapchain->raiiHandle().acquireNextImage(
          std::numeric_limits<std::uint64_t>::max(), frameSync->imageAvailable(slot), nullptr);
      if (acquired.result == ::vk::Result::eErrorOutOfDateKHR) {
        (void)tryRecreate();
        return;
      }
      if (acquired.result != ::vk::Result::eSuccess && acquired.result != ::vk::Result::eSuboptimalKHR) {
        YR_LOG_ERROR("render.vk.backend", "acquireNextImage 失败：" + ::vk::to_string(acquired.result));
        return;
      }

      raiiDevice.resetFences(frameSync->inFlight(slot));
      const bool acquiredSuboptimal = acquired.result == ::vk::Result::eSuboptimalKHR;
      const std::uint32_t imageIndex = acquired.value;
      const ::vk::Image image = swapchain->image(imageIndex);
      const ::vk::Extent2D extent = swapchain->extent();
      const ::vk::CommandBuffer cmd = commandPool->commandBuffer(slot);
      cmd.reset();
      cmd.begin(::vk::CommandBufferBeginInfo{});

      const ::vk::ImageSubresourceRange colorRange{::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
      ::vk::ImageMemoryBarrier2 toColorAttachment{};
      toColorAttachment.setSrcStageMask(::vk::PipelineStageFlagBits2::eColorAttachmentOutput)
          .setSrcAccessMask(::vk::AccessFlagBits2::eNone)
          .setDstStageMask(::vk::PipelineStageFlagBits2::eColorAttachmentOutput)
          .setDstAccessMask(::vk::AccessFlagBits2::eColorAttachmentWrite)
          .setOldLayout(::vk::ImageLayout::eUndefined)
          .setNewLayout(::vk::ImageLayout::eColorAttachmentOptimal)
          .setSrcQueueFamilyIndex(::vk::QueueFamilyIgnored)
          .setDstQueueFamilyIndex(::vk::QueueFamilyIgnored)
          .setImage(image)
          .setSubresourceRange(colorRange);
      ::vk::DependencyInfo dependency{};
      dependency.setImageMemoryBarriers(toColorAttachment);
      cmd.pipelineBarrier2(dependency);

      if (!depthInitialized[slot]) {
        ::vk::ImageMemoryBarrier2 toDepthAttachment{};
        toDepthAttachment.setSrcStageMask(::vk::PipelineStageFlagBits2::eNone)
            .setSrcAccessMask(::vk::AccessFlagBits2::eNone)
            .setDstStageMask(::vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                             ::vk::PipelineStageFlagBits2::eLateFragmentTests)
            .setDstAccessMask(::vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                              ::vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
            .setOldLayout(::vk::ImageLayout::eUndefined)
            .setNewLayout(::vk::ImageLayout::eDepthAttachmentOptimal)
            .setSrcQueueFamilyIndex(::vk::QueueFamilyIgnored)
            .setDstQueueFamilyIndex(::vk::QueueFamilyIgnored)
            .setImage(depthBuffers[slot].image())
            .setSubresourceRange(::vk::ImageSubresourceRange{::vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
        dependency.setImageMemoryBarriers(toDepthAttachment);
        cmd.pipelineBarrier2(dependency);
      }

      ::vk::RenderingAttachmentInfo colorAttachment{};
      colorAttachment.setImageView(swapchain->imageView(imageIndex))
          .setImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
          .setLoadOp(::vk::AttachmentLoadOp::eClear)
          .setStoreOp(::vk::AttachmentStoreOp::eStore)
          .setClearValue(::vk::ClearValue{::vk::ClearColorValue{kClearColor}});
      ::vk::RenderingAttachmentInfo depthAttachment{};
      depthAttachment.setImageView(depthBuffers[slot].imageView())
          .setImageLayout(::vk::ImageLayout::eDepthAttachmentOptimal)
          .setLoadOp(::vk::AttachmentLoadOp::eClear)
          .setStoreOp(::vk::AttachmentStoreOp::eDontCare)
          .setClearValue(::vk::ClearValue{::vk::ClearDepthStencilValue{1.0F, 0}});
      ::vk::RenderingInfo renderingInfo{};
      renderingInfo.setRenderArea(::vk::Rect2D{{0, 0}, extent})
          .setLayerCount(1)
          .setColorAttachments(colorAttachment)
          .setPDepthAttachment(&depthAttachment);
      cmd.beginRendering(renderingInfo);
      cmd.bindPipeline(::vk::PipelineBindPoint::eGraphics, meshPipeline->handle());
      const std::array<::vk::DescriptorSet, 1> boundSets{*descriptorSets[slot]};
      cmd.bindDescriptorSets(::vk::PipelineBindPoint::eGraphics, meshPipeline->layout(), 0, boundSets, {});
      cmd.setViewport(0, ::vk::Viewport{0.0F, static_cast<float>(extent.height), static_cast<float>(extent.width),
                                        -static_cast<float>(extent.height), 0.0F, 1.0F});
      cmd.setScissor(0, ::vk::Rect2D{{0, 0}, extent});

      for (const DrawItem& item : snapshot.items) {
        const auto meshIt = meshes.find(item.mesh.value);
        const auto materialIt = materials.find(item.material.value);
        if (meshIt == meshes.end() || materialIt == materials.end()) {
          ++stats.missing_assets;
          continue;
        }
        const GpuMesh& mesh = meshIt->second;
        const GpuMaterial& material = materialIt->second;
        const auto textureIt = textures.find(material.baseColorTexture);
        const ::vk::DescriptorSet textureSet =
            textureIt == textures.end() ? *defaultTexture->descriptorSet : *textureIt->second.descriptorSet;
        const std::array<::vk::DescriptorSet, 1> textureSets{textureSet};
        cmd.bindDescriptorSets(::vk::PipelineBindPoint::eGraphics, meshPipeline->layout(), 1, textureSets, {});
        const std::array<::vk::Buffer, 1> vertexBuffers{*mesh.buffer};
        const std::array<::vk::DeviceSize, 1> offsets{0};
        cmd.bindVertexBuffers(0, vertexBuffers, offsets);
        cmd.bindIndexBuffer(*mesh.buffer, mesh.vertexBytes, ::vk::IndexType::eUint32);

        vk::MeshPushConstants push{};
        push.model = toMat4(item.world).m_;
        push.baseColorFactor = material.baseColorFactor;
        push.materialParams = {material.metallic, material.roughness, 0.0F, 0.0F};
        cmd.pushConstants<vk::MeshPushConstants>(
            meshPipeline->layout(), ::vk::ShaderStageFlagBits::eVertex | ::vk::ShaderStageFlagBits::eFragment, 0, push);
        cmd.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
        ++stats.draw_calls;
        stats.triangles += mesh.indexCount / 3;
      }
      cmd.endRendering();

      ::vk::ImageMemoryBarrier2 toPresent{};
      toPresent.setSrcStageMask(::vk::PipelineStageFlagBits2::eColorAttachmentOutput)
          .setSrcAccessMask(::vk::AccessFlagBits2::eColorAttachmentWrite)
          .setDstStageMask(::vk::PipelineStageFlagBits2::eNone)
          .setDstAccessMask(::vk::AccessFlagBits2::eNone)
          .setOldLayout(::vk::ImageLayout::eColorAttachmentOptimal)
          .setNewLayout(::vk::ImageLayout::ePresentSrcKHR)
          .setSrcQueueFamilyIndex(::vk::QueueFamilyIgnored)
          .setDstQueueFamilyIndex(::vk::QueueFamilyIgnored)
          .setImage(image)
          .setSubresourceRange(colorRange);
      dependency.setImageMemoryBarriers(toPresent);
      cmd.pipelineBarrier2(dependency);
      cmd.end();

      ::vk::SemaphoreSubmitInfo waitSemaphore{};
      waitSemaphore.setSemaphore(frameSync->imageAvailable(slot))
          .setStageMask(::vk::PipelineStageFlagBits2::eColorAttachmentOutput);
      ::vk::CommandBufferSubmitInfo commandBufferInfo{};
      commandBufferInfo.setCommandBuffer(cmd);
      ::vk::SemaphoreSubmitInfo signalSemaphore{};
      signalSemaphore.setSemaphore(frameSync->renderFinished(imageIndex))
          .setStageMask(::vk::PipelineStageFlagBits2::eColorAttachmentOutput);
      ::vk::SubmitInfo2 submitInfo{};
      submitInfo.setWaitSemaphoreInfos(waitSemaphore)
          .setCommandBufferInfos(commandBufferInfo)
          .setSignalSemaphoreInfos(signalSemaphore);
      device->graphicsQueue().submit2(submitInfo, frameSync->inFlight(slot));
      depthInitialized[slot] = true;

      const ::vk::SwapchainKHR swapchainHandle = swapchain->handle();
      const ::vk::Semaphore renderFinished = frameSync->renderFinished(imageIndex);
      const ::vk::PresentInfoKHR presentInfo{1, &renderFinished, 1, &swapchainHandle, &imageIndex, nullptr, nullptr};
      const ::vk::Result presentResult = device->presentQueue().presentKHR(presentInfo);
      frameSlot_ = (frameSlot_ + 1) % frameSync->framesInFlight();

      if (presentResult != ::vk::Result::eSuccess && presentResult != ::vk::Result::eSuboptimalKHR &&
          presentResult != ::vk::Result::eErrorOutOfDateKHR) {
        YR_LOG_ERROR("render.vk.backend", "presentKHR 失败：" + ::vk::to_string(presentResult));
        return;
      }
      if (acquiredSuboptimal || presentResult == ::vk::Result::eErrorOutOfDateKHR ||
          presentResult == ::vk::Result::eSuboptimalKHR) {
        (void)tryRecreate();
      }
    }
  };

  VulkanBackend::VulkanBackend() : impl_(std::make_unique<Impl>()) {}

  VulkanBackend::~VulkanBackend() {
    shutdown();
  }

  bool VulkanBackend::initialize(const BackendConfig& /*config*/) {
    try {
      impl_->window.emplace();
      impl_->instance.emplace();
      impl_->surface.emplace(*impl_->instance, *impl_->window);
      impl_->device.emplace(*impl_->instance, *impl_->surface);
      impl_->swapchain.emplace(*impl_->device, *impl_->surface, *impl_->window);
      impl_->createDepthBuffers();
      impl_->meshPipeline.emplace(*impl_->device, impl_->swapchain->format(), kDepthFormat);
      impl_->commandPool.emplace(*impl_->device, kFramesInFlight);
      impl_->frameSync.emplace(*impl_->device, kFramesInFlight, impl_->swapchain->imageCount());
      ::vk::SamplerCreateInfo samplerInfo{};
      samplerInfo.setMagFilter(::vk::Filter::eLinear)
          .setMinFilter(::vk::Filter::eLinear)
          .setMipmapMode(::vk::SamplerMipmapMode::eLinear)
          .setAddressModeU(::vk::SamplerAddressMode::eRepeat)
          .setAddressModeV(::vk::SamplerAddressMode::eRepeat)
          .setAddressModeW(::vk::SamplerAddressMode::eRepeat)
          .setMinLod(0.0F)
          .setMaxLod(0.0F);
      impl_->sampler.emplace(impl_->device->raiiDevice(), samplerInfo);
      impl_->uploadCommandPool.emplace(*impl_->device, 1);
      impl_->createFrameResources();
      impl_->createDefaultTexture();
    } catch (const std::exception& error) {
      YR_LOG_ERROR("render.vk.backend", std::string("初始化失败：") + error.what());
      impl_->lastValidationErrors = 0;
      impl_->shutdown();
      return false;
    }
    impl_->stats = BackendStats{};
    YR_LOG_INFO("render.vk.backend", "初始化完成 framesInFlight=" + std::to_string(kFramesInFlight) +
                                         " imageCount=" + std::to_string(impl_->swapchain->imageCount()));
    return true;
  }

  void VulkanBackend::shutdown() {
    impl_->shutdown();
  }

  void VulkanBackend::present(const RenderSnapshot& snapshot) {
    impl_->stats = BackendStats{};
    impl_->stats.items = snapshot.items.size();
    impl_->stats.snapshot_bytes = snapshotByteSize(snapshot);
    if (impl_->device.has_value()) {
      impl_->processResourceChanges(snapshot);
    }
    if (!impl_->window.has_value() || !impl_->swapchain.has_value() || !impl_->device.has_value() ||
        !impl_->surface.has_value() || !impl_->commandPool.has_value()) {
      return;
    }
    if (impl_->window->width() <= 0 || impl_->window->height() <= 0) {
      return;
    }
    if (!impl_->frameSync.has_value() || !impl_->meshPipeline.has_value()) {
      (void)impl_->tryRecreate();
      return;
    }
    if (impl_->swapchain->needsRecreate(*impl_->device, *impl_->surface, *impl_->window)) {
      if (!impl_->tryRecreate()) {
        return;
      }
    }
    impl_->renderFrame(snapshot);
  }

  const char* VulkanBackend::name() const noexcept {
    return "vulkan";
  }

  BackendStats VulkanBackend::last_frame_stats() const noexcept {
    return impl_->stats;
  }

  void VulkanBackend::pollEvents() {
    if (impl_->window.has_value()) {
      impl_->window->pollEvents();
    }
  }

  bool VulkanBackend::shouldClose() const noexcept {
    return !impl_->window.has_value() || impl_->window->shouldClose();
  }

  bool VulkanBackend::isKeyDown(Key key) const noexcept {
    if (!impl_->window.has_value()) {
      return false;
    }
    int glfwKey = GLFW_KEY_UNKNOWN;
    switch (key) {
    case Key::W:
      glfwKey = GLFW_KEY_W;
      break;
    case Key::A:
      glfwKey = GLFW_KEY_A;
      break;
    case Key::S:
      glfwKey = GLFW_KEY_S;
      break;
    case Key::D:
      glfwKey = GLFW_KEY_D;
      break;
    }
    return glfwGetKey(impl_->window->nativeHandle(), glfwKey) == GLFW_PRESS;
  }

  std::uint32_t VulkanBackend::validationErrorCount() const noexcept {
    if (impl_->instance.has_value()) {
      return impl_->instance->debugStats().errors;
    }
    return impl_->lastValidationErrors;
  }

  std::unique_ptr<RendererBackend> create_vulkan_backend() {
    return std::make_unique<VulkanBackend>();
  }

} // namespace yr::render
