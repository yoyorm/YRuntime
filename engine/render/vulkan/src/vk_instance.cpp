#include "vk_instance.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include <yr/core/log.h>

namespace yr::render::vk {

  namespace {

#ifdef YR_ENABLE_VULKAN_VALIDATION
    constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";
    constexpr const char* kValidationHint =
        "请先 sudo apt install vulkan-validationlayers；或 source ~/SDK/Vulkan/1.4.357.1/setup-env.sh"
        "（后者设置 VK_ADD_LAYER_PATH）";

    [[nodiscard]] bool isInstanceLayerAvailable(const char* layerName) {
      for (const ::vk::LayerProperties& properties : ::vk::enumerateInstanceLayerProperties()) {
        if (std::strcmp(properties.layerName.data(), layerName) == 0) {
          return true;
        }
      }
      return false;
    }
#endif

    [[nodiscard]] std::string joinNames(const std::vector<std::string>& names) {
      std::string joined = "[";
      for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
          joined += ", ";
        }
        joined += names[i];
      }
      joined += "]";
      return joined;
    }

    [[nodiscard]] std::vector<const char*> toNamePointers(const std::vector<std::string>& names) {
      std::vector<const char*> pointers;
      pointers.reserve(names.size());
      for (const std::string& name : names) {
        pointers.push_back(name.c_str());
      }
      return pointers;
    }

  } // namespace

  Instance::Instance() {
#ifdef YR_ENABLE_VULKAN_VALIDATION
    validationLayerAvailable_ = isInstanceLayerAvailable(kValidationLayerName);
    if (validationLayerAvailable_) {
      enabledLayers_.emplace_back(kValidationLayerName);
      validationEnabled_ = true;
      YR_LOG_INFO("render.vk.instance", "validation 层已启用：VK_LAYER_KHRONOS_validation");
    } else {
      throw std::runtime_error(std::string("loader 看不到 VK_LAYER_KHRONOS_validation；") + kValidationHint);
    }
#else
    YR_LOG_INFO("render.vk.instance", "validation 层未启用（非 Debug 构建）");
#endif

    // glfwGetRequiredInstanceExtensions 依赖 glfwInit，故 Window 必须先构造。
    std::uint32_t glfwExtensionCount = 0;
    const char* const* glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensions == nullptr) {
      throw std::runtime_error("glfwGetRequiredInstanceExtensions failed");
    }
    for (std::uint32_t i = 0; i < glfwExtensionCount; ++i) {
      enabledExtensions_.emplace_back(glfwExtensions[i]);
    }
    if (validationEnabled_) {
      enabledExtensions_.emplace_back(::vk::EXTDebugUtilsExtensionName);
    }

    const std::vector<const char*> layerNames = toNamePointers(enabledLayers_);
    const std::vector<const char*> extensionNames = toNamePointers(enabledExtensions_);

    const ::vk::ApplicationInfo applicationInfo{"yr_render_demo", ::vk::makeApiVersion(0, 1, 0, 0), "YRuntime",
                                                ::vk::makeApiVersion(0, 1, 0, 0), ::vk::ApiVersion13};
    ::vk::InstanceCreateInfo createInfo{};
    createInfo.setPApplicationInfo(&applicationInfo);
    createInfo.setPEnabledLayerNames(layerNames);
    createInfo.setPEnabledExtensionNames(extensionNames);

    instance_ = context_.createInstance(createInfo);

    if (validationEnabled_) {
      ::vk::DebugUtilsMessengerCreateInfoEXT messengerInfo{};
      messengerInfo.setMessageSeverity(::vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
                                       ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning);
      messengerInfo.setMessageType(::vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                   ::vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                   ::vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance);
      messengerInfo.setPfnUserCallback(&debugUtilsCallback);
      messengerInfo.setPUserData(&debugStats_);
      debugMessenger_ = instance_.createDebugUtilsMessengerEXT(messengerInfo);
    }

    YR_LOG_INFO("render.vk.instance", "instance 创建成功 layers=" + joinNames(enabledLayers_) +
                                          " extensions=" + joinNames(enabledExtensions_));
  }

} // namespace yr::render::vk