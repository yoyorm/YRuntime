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

    const std::vector<const char*> extensionNames{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    ::vk::DeviceCreateInfo createInfo{};
    createInfo.setQueueCreateInfos(queueInfos);
    createInfo.setPEnabledExtensionNames(extensionNames);

    // raii::Device 需要 raii::PhysicalDevice 作构造参数；物理设备非拥有，故只在此构造期借用。
    device_ = ::vk::raii::Device(::vk::raii::PhysicalDevice(instance.raiiInstance(), physicalDevice_), createInfo);

    graphicsQueue_ = device_.getQueue(queues_.graphics, 0);
    presentQueue_ = device_.getQueue(queues_.present, 0);

    YR_LOG_INFO("render.vk.device", "逻辑设备创建成功 graphicsFamily=" + std::to_string(queues_.graphics) +
                                        " presentFamily=" + std::to_string(queues_.present));
  }

} // namespace yr::render::vk