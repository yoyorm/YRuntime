#include "vk_device_selection.h"

#include <string>
#include <string_view>
#include <vector>

#include <yr/core/log.h>

namespace yr::render::vk {

  bool QueueFamilySelection::valid() const noexcept {
    return graphics != kInvalidIndex && present != kInvalidIndex;
  }

  bool QueueFamilySelection::sameFamily() const noexcept {
    return valid() && graphics == present;
  }

  bool PhysicalDeviceSelection::valid() const noexcept {
    return deviceIndex != kInvalidIndex && queues.valid();
  }

  namespace {

    [[nodiscard]] bool hasExtension(const ::vk::PhysicalDevice& device, const char* name) {
      const std::string_view wanted{name};
      for (const ::vk::ExtensionProperties& property : device.enumerateDeviceExtensionProperties()) {
        if (std::string_view(property.extensionName) == wanted) {
          return true;
        }
      }
      return false;
    }

    // 独显 > 核显 > 虚拟 > 其它非 CPU；数值越大越优先。
    [[nodiscard]] int priorityOf(::vk::PhysicalDeviceType type) {
      switch (type) {
      case ::vk::PhysicalDeviceType::eDiscreteGpu:
        return 3;
      case ::vk::PhysicalDeviceType::eIntegratedGpu:
        return 2;
      case ::vk::PhysicalDeviceType::eVirtualGpu:
        return 1;
      default:
        return 0;
      }
    }

    [[nodiscard]] std::string decodeApiVersion(std::uint32_t apiVersion) {
      return std::to_string(::vk::versionMajor(apiVersion)) + "." + std::to_string(::vk::versionMinor(apiVersion)) +
             "." + std::to_string(::vk::versionPatch(apiVersion));
    }

  } // namespace

  PhysicalDeviceSelection select_physical_device(std::span<const ::vk::PhysicalDevice> devices,
                                                 ::vk::SurfaceKHR surface) {
    // 规则（按顺序）：
    // 1. 拒绝 eCpu（llvmpipe 等软件渲染）。
    // 2. 拒绝 apiVersion < 1.3（instance 请求的是 1.3）。
    // 3. 拒绝扩展列表里没有 VK_KHR_swapchain 的设备。
    // 4. graphics 族 = 第一个 queueCount > 0 且含 eGraphics 的族。
    // 5. present 族 = graphics 族自身支持 surface 时用它，否则第一个 queueCount > 0 且支持 surface 的族。
    // 6. 优先级 独显 > 核显 > 虚拟 > 其它非 CPU；同优先级取先出现者。
    // 7. 任一必需条件缺失即拒绝，并把原因写进日志。

    PhysicalDeviceSelection best{};
    int bestPriority = -1;

    for (std::uint32_t index = 0; index < devices.size(); ++index) {
      const ::vk::PhysicalDevice& device = devices[index];
      const ::vk::PhysicalDeviceProperties properties = device.getProperties();
      const std::vector<::vk::QueueFamilyProperties> families = device.getQueueFamilyProperties();
      const bool swapchain = hasExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

      const std::string line =
          "device[" + std::to_string(index) + "] name=" + std::string(properties.deviceName) +
          " type=" + ::vk::to_string(properties.deviceType) + " apiVersion=" + decodeApiVersion(properties.apiVersion) +
          " driverVersion=" + std::to_string(properties.driverVersion) +
          " queueFamilies=" + std::to_string(families.size()) + " swapchain=" + (swapchain ? "yes" : "no");

      std::string rejection;
      if (properties.deviceType == ::vk::PhysicalDeviceType::eCpu) {
        rejection = "CPU 设备（软件渲染）";
      } else if (properties.apiVersion < ::vk::ApiVersion13) {
        rejection = "apiVersion < 1.3";
      } else if (!swapchain) {
        rejection = "缺少 VK_KHR_swapchain";
      }

      QueueFamilySelection queues{};
      if (rejection.empty()) {
        for (std::uint32_t family = 0; family < families.size(); ++family) {
          if (families[family].queueCount > 0 &&
              static_cast<bool>(families[family].queueFlags & ::vk::QueueFlagBits::eGraphics)) {
            queues.graphics = family;
            break;
          }
        }
        if (queues.graphics == kInvalidIndex) {
          rejection = "没有 queueCount > 0 的 graphics 族";
        }
      }

      if (rejection.empty()) {
        if (device.getSurfaceSupportKHR(queues.graphics, surface) == ::vk::True) {
          queues.present = queues.graphics;
        } else {
          for (std::uint32_t family = 0; family < families.size(); ++family) {
            if (families[family].queueCount > 0 && device.getSurfaceSupportKHR(family, surface) == ::vk::True) {
              queues.present = family;
              break;
            }
          }
          if (queues.present == kInvalidIndex) {
            rejection = "没有支持 surface 的 present 族";
          }
        }
      }

      if (rejection.empty()) {
        const int priority = priorityOf(properties.deviceType);
        if (priority > bestPriority) {
          best.deviceIndex = index;
          best.queues = queues;
          bestPriority = priority;
          YR_LOG_INFO("render.vk.select", line + " -> accepted");
          continue;
        }
        rejection = "优先级不高于已选设备";
      }

      YR_LOG_INFO("render.vk.select", line + " -> rejected: " + rejection);
    }

    if (best.valid()) {
      const ::vk::PhysicalDeviceProperties properties = devices[best.deviceIndex].getProperties();
      YR_LOG_INFO("render.vk.select",
                  "选中 device[" + std::to_string(best.deviceIndex) + "] name=" + std::string(properties.deviceName) +
                      " graphicsFamily=" + std::to_string(best.queues.graphics) + " presentFamily=" +
                      std::to_string(best.queues.present) + " sameFamily=" + (best.queues.sameFamily() ? "yes" : "no"));
    }
    return best;
  }

} // namespace yr::render::vk