#include "vk_device.h"

#include <stdexcept>
#include <string>
#include <vector>

#include <yr/core/log.h>

#include "vk_instance.h"
#include "vk_surface.h"

namespace yr::render::vk {

  Device::Device(const Instance& instance, const Surface& surface) {
    const std::vector<::vk::PhysicalDevice> devices = instance.handle().enumeratePhysicalDevices();
    const PhysicalDeviceSelection selection = select_physical_device(devices, surface.handle());
    if (!selection.valid()) {
      throw std::runtime_error("没有可用的 Vulkan 设备（详见 render.vk.select 日志）");
    }

    physicalDevice_ = devices[selection.deviceIndex];
    queues_ = selection.queues;

    // 按 distinct family 建队列创建信息：同族只建一条、queueCount = 1。
    // priority 的地址要在 createDevice 返回前保持有效。
    const float priority = 1.0f;
    std::vector<::vk::DeviceQueueCreateInfo> queueInfos;
    queueInfos.push_back(::vk::DeviceQueueCreateInfo{}
                             .setQueueFamilyIndex(queues_.graphics)
                             .setQueueCount(1)
                             .setPQueuePriorities(&priority));
    if (queues_.present != queues_.graphics) {
      queueInfos.push_back(::vk::DeviceQueueCreateInfo{}
                               .setQueueFamilyIndex(queues_.present)
                               .setQueueCount(1)
                               .setPQueuePriorities(&priority));
    }

    // 1.3 core feature 先查再开：dynamic rendering 与 synchronization2 都直接决定本层能否工作。
    const auto available =
        physicalDevice_.getFeatures2<::vk::PhysicalDeviceFeatures2, ::vk::PhysicalDeviceVulkan13Features>();
    const ::vk::PhysicalDeviceVulkan13Features& available13 = available.get<::vk::PhysicalDeviceVulkan13Features>();
    if (!available13.dynamicRendering) {
      throw std::runtime_error("设备不支持 dynamicRendering（VkPhysicalDeviceVulkan13Features）");
    }
    if (!available13.synchronization2) {
      throw std::runtime_error("设备不支持 synchronization2（VkPhysicalDeviceVulkan13Features）");
    }

    const std::vector<const char*> extensionNames{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // chain 必须活到 createDevice 返回：DeviceCreateInfo::pNext 指向链上的 feature 结构。
    ::vk::StructureChain<::vk::DeviceCreateInfo, ::vk::PhysicalDeviceVulkan13Features> chain;
    ::vk::DeviceCreateInfo& createInfo = chain.get<::vk::DeviceCreateInfo>();
    createInfo.setQueueCreateInfos(queueInfos);
    createInfo.setPEnabledExtensionNames(extensionNames);
    ::vk::PhysicalDeviceVulkan13Features& features13 = chain.get<::vk::PhysicalDeviceVulkan13Features>();
    features13.setDynamicRendering(::vk::True).setSynchronization2(::vk::True);

    // raii::Device 需要 raii::PhysicalDevice 作构造参数；物理设备非拥有，故只在此构造期借用。
    device_ = ::vk::raii::Device(::vk::raii::PhysicalDevice(instance.raiiInstance(), physicalDevice_), createInfo);

    graphicsQueue_ = device_.getQueue(queues_.graphics, 0);
    presentQueue_ = device_.getQueue(queues_.present, 0);

    YR_LOG_INFO("render.vk.device", "逻辑设备创建成功 graphicsFamily=" + std::to_string(queues_.graphics) +
                                        " presentFamily=" + std::to_string(queues_.present) +
                                        " dynamicRendering=on synchronization2=on");
  }

} // namespace yr::render::vk