#include <yr/core/log.h>

#include <cstdio>
#include <mutex>

namespace yr::core {
  namespace {
    std::mutex log_mutex;
    LogHandler g_handler = nullptr;
    LogLevel g_min_level = defaultMinLogLevel();

    const char* levelName(LogLevel level) noexcept {
      switch (level) {
      case LogLevel::kDebug:
        return "DEBUG";
      case LogLevel::kInfo:
        return "INFO";
      case LogLevel::kWarn:
        return "WARN";
      case LogLevel::kError:
        return "ERROR";
      case LogLevel::kFatal:
        return "FATAL";
      }
      return "UNKNOWN";
    }
  } // namespace

  void defaultLogHandler(const LogInfo& info) noexcept {
    // TODO(M0)：时间戳 / 帧号 / 线程 ID（05-engineering §5）
    std::fprintf(stderr, "[%s][%s] %s:%d: %s: ", levelName(info.level), info.tag == nullptr ? "-" : info.tag,
                 info.file == nullptr ? "-" : info.file, info.line, info.func == nullptr ? "-" : info.func);
    std::fwrite(info.message.data(), 1, info.message.size(), stderr);
    std::fputc('\n', stderr);
  }
  void setLogHandler(LogHandler handler) noexcept {
    std::lock_guard lock{log_mutex};
    g_handler = handler;
  }

  LogLevel minLogLevel() noexcept {
    std::lock_guard lock{log_mutex};
    return g_min_level;
  }

  void setMinLogLevel(LogLevel level) noexcept {
    std::lock_guard lock{log_mutex};
    g_min_level = level;
  }

  void reportLog(LogLevel level, const char* tag, const char* file, int line, const char* func,
                 std::string_view message) noexcept {
    std::lock_guard lock{log_mutex};
    if (level < g_min_level) {
      return;
    }

    const LogInfo info{level, tag, file, line, func, message};
    LogHandler handler = (g_handler != nullptr) ? g_handler : &defaultLogHandler;
    handler(info);
  }

} // namespace yr::core
