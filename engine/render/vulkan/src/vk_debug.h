#pragma once

#include <cstdint>

#include <vulkan/vulkan.hpp>

namespace yr::render::vk {

  // validation 层消息计数：error 计数并立即打印，warning 只计数。
  struct DebugStats {
    std::uint32_t errors = 0;
    std::uint32_t warnings = 0;
  };

  // ::vk::DebugUtilsMessengerCallbackEXT 形态；userData 必须指向一个 DebugStats，
  // 其存活期必须覆盖 messenger（见 vk_instance.h 的成员顺序）。
  VKAPI_ATTR ::vk::Bool32 VKAPI_CALL debugUtilsCallback(::vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                        ::vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
                                                        const ::vk::DebugUtilsMessengerCallbackDataEXT* callbackData,
                                                        void* userData);

} // namespace yr::render::vk