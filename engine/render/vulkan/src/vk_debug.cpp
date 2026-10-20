#include "vk_debug.h"

#include <string>

#include <yr/core/log.h>

namespace yr::render::vk {

  VKAPI_ATTR ::vk::Bool32 VKAPI_CALL debugUtilsCallback(::vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                        ::vk::DebugUtilsMessageTypeFlagsEXT /*messageTypes*/,
                                                        const ::vk::DebugUtilsMessengerCallbackDataEXT* callbackData,
                                                        void* userData) {
    auto* stats = static_cast<DebugStats*>(userData);
    if (stats == nullptr || callbackData == nullptr) {
      return ::vk::False;
    }

    if (static_cast<bool>(messageSeverity & ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)) {
      ++stats->errors;
      std::string line = "validation error";
      if (callbackData->pMessageIdName != nullptr) {
        line += " [";
        line += callbackData->pMessageIdName;
        line += "]";
      }
      line += ": ";
      if (callbackData->pMessage != nullptr) {
        line += callbackData->pMessage;
      }
      YR_LOG_ERROR("render.vk.validation", line);
      return ::vk::False;
    }

    if (static_cast<bool>(messageSeverity & ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)) {
      ++stats->warnings;
    }
    return ::vk::False;
  }

} // namespace yr::render::vk