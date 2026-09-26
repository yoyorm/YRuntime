#pragma once

#include <string_view>

// 总开关。默认 1 且不由 NDEBUG 推导：日志 release 下也要保留（05-engineering §5）
#if !defined(YR_ENABLE_LOG)
#define YR_ENABLE_LOG 1
#endif

// DEBUG 级开关：release 下整条剔除
#if !defined(YR_ENABLE_DEBUG_LOG)
#ifdef NDEBUG
#define YR_ENABLE_DEBUG_LOG 0
#else
#define YR_ENABLE_DEBUG_LOG 1
#endif
#endif

namespace yr::core {

  enum class LogLevel : short { kDebug, kInfo, kWarn, kError, kFatal };

  struct LogInfo {
    LogLevel level;
    const char* tag;
    const char* file;
    int line;
    const char* func;
    std::string_view message; // 不拥有数据，仅在 reportLog 返回前有效
  };

  // 处理 LogInfo 信息;
  // handler 会在日志锁内执行；handler 内不得再调用任何日志 API。
  using LogHandler = void (*)(const LogInfo&) noexcept;

  // 默认 handler：打印到 stderr
  void defaultLogHandler(const LogInfo& info) noexcept;

  // 替换 handler。传 nullptr 恢复默认。
  void setLogHandler(LogHandler handler) noexcept;

  // 运行期过滤阈值：低于它的消息在 reportLog 里直接丢弃
  LogLevel minLogLevel() noexcept;
  void setMinLogLevel(LogLevel level) noexcept;

  // 构建配置决定的默认阈值：DEBUG 级编译进来了就该看得见
  constexpr LogLevel defaultMinLogLevel() noexcept {
#if YR_ENABLE_DEBUG_LOG
    return LogLevel::kDebug;
#else
    return LogLevel::kInfo;
#endif
  }

  // 宏调用它：运行期过滤 → 组装 LogInfo → 交给当前 handler
  void reportLog(LogLevel level, const char* tag, const char* file, int line, const char* func,
                 std::string_view message) noexcept;

} // namespace yr::core

#if YR_ENABLE_LOG

// TODO：fmt 风格格式化未实现，暂时只传格式串本身（不转发 __VA_ARGS__）
#define YR_LOG_INFO(tag, fmt)                                                                                          \
  ::yr::core::reportLog(::yr::core::LogLevel::kInfo, (tag), __FILE__, __LINE__, __func__, (fmt))
#define YR_LOG_WARN(tag, fmt)                                                                                          \
  ::yr::core::reportLog(::yr::core::LogLevel::kWarn, (tag), __FILE__, __LINE__, __func__, (fmt))
#define YR_LOG_ERROR(tag, fmt)                                                                                         \
  ::yr::core::reportLog(::yr::core::LogLevel::kError, (tag), __FILE__, __LINE__, __func__, (fmt))
#define YR_LOG_FATAL(tag, fmt)                                                                                         \
  ::yr::core::reportLog(::yr::core::LogLevel::kFatal, (tag), __FILE__, __LINE__, __func__, (fmt))

#if YR_ENABLE_DEBUG_LOG
#define YR_LOG_DEBUG(tag, fmt)                                                                                         \
  ::yr::core::reportLog(::yr::core::LogLevel::kDebug, (tag), __FILE__, __LINE__, __func__, (fmt))
#else
#define YR_LOG_DEBUG(tag, fmt) ((void)0) // release：整条消失
#endif

#else // YR_ENABLE_LOG == 0：全部编译期消失

#define YR_LOG_DEBUG(tag, fmt) ((void)0)
#define YR_LOG_INFO(tag, fmt) ((void)0)
#define YR_LOG_WARN(tag, fmt) ((void)0)
#define YR_LOG_ERROR(tag, fmt) ((void)0)
#define YR_LOG_FATAL(tag, fmt) ((void)0)

#endif
