#include "vk_depth_buffer.h"

#include <stdexcept>

#include "vk_device.h"

namespace yr::render::vk {

  namespace {

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

  DepthBuffer::DepthBuffer(const Device& device, ::vk::Extent2D extent, ::vk::Format format) : format_(format) {
    const ::vk::raii::Device& raiiDevice = device.raiiDevice();
    ::vk::ImageCreateInfo imageInfo{};
    imageInfo.setImageType(::vk::ImageType::e2D)
        .setFormat(format)
        .setExtent(::vk::Extent3D{extent.width, extent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(::vk::SampleCountFlagBits::e1)
        .setTiling(::vk::ImageTiling::eOptimal)
        .setUsage(::vk::ImageUsageFlagBits::eDepthStencilAttachment)
        .setSharingMode(::vk::SharingMode::eExclusive)
        // Vulkan image creation only permits UNDEFINED/PREINITIALIZED; first use transitions to depth attachment.
        .setInitialLayout(::vk::ImageLayout::eUndefined);
    image_ = ::vk::raii::Image(raiiDevice, imageInfo);

    const ::vk::MemoryRequirements requirements = image_.getMemoryRequirements();
    ::vk::MemoryAllocateInfo allocateInfo{};
    allocateInfo.setAllocationSize(requirements.size)
        .setMemoryTypeIndex(findMemoryType(device.physicalDevice(), requirements.memoryTypeBits,
                                           ::vk::MemoryPropertyFlagBits::eDeviceLocal));
    memory_ = ::vk::raii::DeviceMemory(raiiDevice, allocateInfo);
    image_.bindMemory(*memory_, 0);

    ::vk::ImageViewCreateInfo viewInfo{};
    viewInfo.setImage(*image_)
        .setViewType(::vk::ImageViewType::e2D)
        .setFormat(format)
        .setSubresourceRange(::vk::ImageSubresourceRange{::vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
    view_ = ::vk::raii::ImageView(raiiDevice, viewInfo);
  }

} // namespace yr::render::vk
