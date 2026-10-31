// headless 正式运行入口：解析 --frames N / --scene PATH，创建 Engine 跑 N 帧并报告 FrameStats / 存活对象。
// 无 --scene 时保持空场景；有 --scene 时从文本 .scn 加载。

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <yr/core/log.h>
#include <yr/core/timer.h>
#include <yr/engine/engine.h>
#include <yr/engine/frame_stats.h>
#include <yr/object/class_db.h>
#include <yr/object/object_db.h>
#include <yr/render/backend.h>
#include <yr/render/null_backend.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_diagnostic.h>
#include <yr/scene/scene_reader.h>

namespace {

  struct Options {
    std::uint64_t frames = 1000;
    std::string scenePath;
    bool help = false;
  };

  void printUsage(std::FILE* stream, const char* program) {
    std::fprintf(stream, "usage: %s [--frames N] [--scene PATH]\n", program);
  }

  // 手写参数解析：只支持帧数、场景路径与 help，不引入 CLI 库。
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
      } else if (arg == "--scene") {
        if (i + 1 >= argc) {
          return false;
        }
        options.scenePath = argv[++i];
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
  // backend 由本入口拥有，生命周期覆盖 engine.run()；Engine 只持非拥有指针。
  auto backend = yr::render::create_null_backend();
  engine.setBackend(backend.get());

  std::size_t nodeCount = 0;
  std::uint64_t sceneBytes = 0;
  std::uint64_t loadUs = 0;

  if (!options.scenePath.empty()) {
    yr::scene::register_scene_classes();

    std::ifstream sceneStream(options.scenePath, std::ios::binary);
    if (!sceneStream) {
      std::fprintf(stderr, "cannot open scene: %s\n", options.scenePath.c_str());
      return 1;
    }

    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(options.scenePath, sizeError);
    if (!sizeError) {
      sceneBytes = static_cast<std::uint64_t>(fileSize);
    }

    const auto loadStart = std::chrono::steady_clock::now();
    const yr::scene::SceneReader reader(yr::obj::globalClassDB());
    const yr::scene::SceneLoadResult loaded = reader.read(sceneStream);

    for (const yr::scene::SceneDiagnostic& diagnostic : loaded.diagnostics) {
      const char* severity = diagnostic.severity == yr::scene::SceneDiagnostic::Severity::kError ? "error" : "warning";
      std::fprintf(stderr, "%s:%d:%d: %s: %s\n", options.scenePath.c_str(), diagnostic.line, diagnostic.column,
                   severity, diagnostic.message.c_str());
    }
    if (loaded.hasErrors()) {
      return 1;
    }

    nodeCount = loaded.scene.nodes.size();
    std::unique_ptr<yr::scene::Node> root = loaded.scene.instantiate(yr::obj::globalClassDB());
    if (root == nullptr) {
      std::fprintf(stderr, "cannot instantiate scene: %s\n", options.scenePath.c_str());
      return 1;
    }
    if (!engine.sceneTree().setRoot(std::move(root))) {
      std::fprintf(stderr, "cannot set scene root: %s\n", options.scenePath.c_str());
      return 1;
    }

    const auto loadEnd = std::chrono::steady_clock::now();
    loadUs =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(loadEnd - loadStart).count());
  }

  engine.run(options.frames);

  const yr::engine::FrameStats& stats = engine.mainLoop().stats();
  const yr::render::BackendStats backendStats = backend->last_frame_stats();
  std::string summary = "frames=" + std::to_string(engine.mainLoop().frameCount());
  summary += " physics_steps=" + std::to_string(stats.totalPhysicsSteps());
  summary += " alive=" + std::to_string(yr::obj::globalObjectDB().aliveCount());
  summary += " items=" + std::to_string(backendStats.items);
  summary += " draw_calls=" + std::to_string(backendStats.draw_calls);
  summary += " triangles=" + std::to_string(backendStats.triangles);
  summary += " | total p50=" + formatDuration(stats.totalP50());
  summary += " p95=" + formatDuration(stats.totalP95());
  summary += " max=" + formatDuration(stats.totalMax());
  summary += " | scene=" + (options.scenePath.empty() ? std::string("-") : options.scenePath);
  summary += " nodes=" + std::to_string(nodeCount);
  summary += " scene_bytes=" + std::to_string(sceneBytes);
  summary += " load_us=" + std::to_string(loadUs);
  YR_LOG_INFO("runtime", summary);

  return 0;
}