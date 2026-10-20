// render_demo 正式运行入口：M6 验证 Vulkan 后端外壳，M7 长成 snapshot 驱动的演示。

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>

#include <yr/core/log.h>
#include <yr/render/vk/vulkan_backend.h>

namespace {

  struct Options {
    std::uint64_t frames = 0; // 0 = 直到窗口关闭
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

  // backend 由本入口拥有，生命周期覆盖主循环；M7 改为交给 MainLoop 驱动。
  yr::render::VulkanBackend backend;
  if (!backend.initialize({})) {
    return 1;
  }

  std::uint64_t frameIndex = 0;
  while (!backend.shouldClose()) {
    backend.present({});
    ++frameIndex;
    if (options.frames != 0 && frameIndex >= options.frames) {
      break;
    }
  }
  backend.shutdown();

  const std::uint32_t errors = backend.validationErrorCount();
  YR_LOG_INFO("render_demo", "frames=" + std::to_string(frameIndex) + " validation_errors=" + std::to_string(errors));
  return errors == 0 ? 0 : 1;
}