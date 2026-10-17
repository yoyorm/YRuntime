// headless 正式运行入口：解析 --frames N，创建 Engine 跑 N 帧并报告 FrameStats / 存活对象。
// 不生成场景或演示节点；装配由 Engine 负责。

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>

#include <yr/core/log.h>
#include <yr/core/timer.h>
#include <yr/engine/engine.h>
#include <yr/engine/frame_stats.h>
#include <yr/object/object_db.h>

namespace {

  struct Options {
    std::uint64_t frames = 1000;
    bool help = false;
  };

  void printUsage(std::FILE* stream, const char* program) {
    std::fprintf(stream, "usage: %s [--frames N]\n", program);
  }

  // 手写参数解析：只支持帧数与 help，不引入 CLI 库。
  [[nodiscard]] bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
      const std::string_view arg = argv[i];
      if (arg == "--frames") {
        if (i + 1 >= argc) {
          return false;
        }
        // std::from_chars：无异常、精确拒绝非法输入。
        std::uint64_t value = 0;
        const std::string_view text = argv[++i];
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
          return false;
        }
        options.frames = value;
      } else if (arg == "--help" || arg == "-h") {
        options.help = true;
      } else {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] std::string formatDuration(yr::core::Duration duration) {
    const long long nanoseconds = duration.count();
    char buffer[32] = {};
    if (nanoseconds < 1000) {
      std::snprintf(buffer, sizeof(buffer), "%lldns", nanoseconds);
    } else if (nanoseconds < 1000000) {
      std::snprintf(buffer, sizeof(buffer), "%.1fus", static_cast<double>(nanoseconds) / 1000.0);
    } else {
      std::snprintf(buffer, sizeof(buffer), "%.1fms", static_cast<double>(nanoseconds) / 1000000.0);
    }
    return std::string(buffer);
  }

} // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parseArgs(argc, argv, options)) {
    printUsage(stderr, argv[0]);
    return 2;
  }
  if (options.help) {
    printUsage(stdout, argv[0]);
    return 0;
  }

  yr::engine::Engine engine;
  engine.run(options.frames);

  const yr::engine::FrameStats& stats = engine.mainLoop().stats();
  std::string summary = "frames=" + std::to_string(engine.mainLoop().frameCount());
  summary += " physics_steps=" + std::to_string(stats.totalPhysicsSteps());
  summary += " alive=" + std::to_string(yr::obj::globalObjectDB().aliveCount());
  summary += " | total p50=" + formatDuration(stats.totalP50());
  summary += " p95=" + formatDuration(stats.totalP95());
  summary += " max=" + formatDuration(stats.totalMax());
  YR_LOG_INFO("runtime", summary);

  return 0;
}