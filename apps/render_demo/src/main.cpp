// render_demo 正式运行入口：M7b 由 MainLoop 驱动、M7c 键盘移动 Node、M7d 快照 dump / replay。
// Vulkan 后端按快照中的 DrawItem 逐个绘制。

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <yr/asset/asset_database.h>
#include <yr/asset/gltf_loader.h>
#include <yr/core/log.h>
#include <yr/core/timer.h>
#include <yr/engine/clock.h>
#include <yr/engine/main_loop.h>
#include <yr/event/event_bus.h>
#include <yr/math/mat4.h>
#include <yr/math/transform.h>
#include <yr/math/vec3.h>
#include <yr/render/snapshot.h>
#include <yr/render/snapshot_io.h>
#include <yr/render/vk/vulkan_backend.h>
#include <yr/scene/mesh_instance_node.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

namespace {

  [[nodiscard]] bool writeBytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
      return false;
    }
    if (!bytes.empty()) {
      file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return file.good();
  }

  // 空文件也返回 true；截断 / 非法格式交给 decodeSnapshot 判定。
  [[nodiscard]] bool readBytes(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !file.bad();
  }

  // 与 runtime_headless 相同的日志格式：ns / us / ms 三档，避免统计摘要刷出长小数。
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

  // host 驱动 MainLoop：阶段 1 泵窗口事件，WASD 小幅平移场景根；关闭窗口转成 requestQuit。
  class DemoLoop final : public yr::engine::MainLoop {
  public:
    DemoLoop(yr::scene::SceneTree& tree, yr::evt::EventBus& events, yr::render::VulkanBackend& backend,
             yr::scene::Node& movable, std::string dumpPath)
        : MainLoop(tree, events), backend_(backend), movable_(movable), dumpPath_(std::move(dumpPath)) {}

    [[nodiscard]] bool dumpFailed() const noexcept { return dumpFailed_; }

  protected:
    void pollInput() override {
      backend_.pollEvents();
      if (backend_.shouldClose()) {
        requestQuit();
        return;
      }

      const float dx = (backend_.isKeyDown(yr::render::Key::D) ? 1.0F : 0.0F) -
                       (backend_.isKeyDown(yr::render::Key::A) ? 1.0F : 0.0F);
      const float dy = (backend_.isKeyDown(yr::render::Key::W) ? 1.0F : 0.0F) -
                       (backend_.isKeyDown(yr::render::Key::S) ? 1.0F : 0.0F);
      if (dx == 0.0F && dy == 0.0F) {
        return; // 无按键不改位置，避免无谓的 transform 失效
      }

      // 长帧（拖窗口 / 断点）会把位移放大成瞬移，clamp dt 到 100ms。
      const float dt = std::clamp(static_cast<float>(delta()), 0.0F, 0.1F);
      constexpr float kSpeed = 0.02F; // units/s
      const yr::math::Vec3& current = movable_.position();
      yr::math::Vec3 next = current + yr::math::Vec3{dx, dy, 0.0F} * (kSpeed * dt);
      next.x = std::clamp(next.x, -0.05F, 0.05F);
      next.y = std::clamp(next.y, -0.05F, 0.05F);
      movable_.setPosition(next);
    }

    // 只 dump 首个进入 present 阶段的快照：颜色按 ObjectID 取，重放同一份数据才能逐像素一致。
    void present() override {
      if (!dumpPath_.empty() && !dumped_ && !dumpFailed_) {
        const std::vector<std::uint8_t> bytes = yr::render::encodeSnapshot(snapshot());
        if (writeBytes(dumpPath_, bytes)) {
          dumped_ = true;
          YR_LOG_INFO("render_demo", "dump_snapshot path=" + dumpPath_ + " bytes=" + std::to_string(bytes.size()) +
                                         " items=" + std::to_string(snapshot().items.size()));
        } else {
          dumpFailed_ = true;
          requestQuit();
          YR_LOG_ERROR("render_demo", "dump_snapshot 写入失败：" + dumpPath_);
        }
      }
      MainLoop::present();
    }

  private:
    yr::render::VulkanBackend& backend_;
    // 非拥有：树由 main 持有，且比 loop 活得更久。
    yr::scene::Node& movable_;
    std::string dumpPath_;
    bool dumped_ = false;
    bool dumpFailed_ = false;
  };

  struct Options {
    std::uint64_t frames = 0; // 0 = 直到窗口关闭
    std::uint64_t dropAt = 0;
    std::string dumpSnapshot;
    std::string replaySnapshot;
    bool help = false;
  };

  [[nodiscard]] yr::render::TransformPod toTransform(const yr::math::Mat4& matrix) noexcept {
    yr::render::TransformPod pod;
    for (std::size_t index = 0; index < 16; ++index) {
      pod.m[index] = matrix.m_[index];
    }
    return pod;
  }

  void printUsage(std::FILE* stream, const char* program) {
    std::fprintf(stream,
                 "usage: %s [--frames N] [--drop-at N] [--dump-snapshot PATH] [--replay-snapshot PATH]\n"
                 "  --dump-snapshot PATH     dump the first presented RenderSnapshot\n"
                 "  --drop-at N              queue the first MeshInstanceNode for deletion after tick N\n"
                 "  --replay-snapshot PATH   replay a dumped snapshot without a SceneTree\n",
                 program);
  }

  // 手写参数解析：只支持帧数、dump/replay 与 help，不引入 CLI 库。
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
      } else if (arg == "--drop-at") {
        if (i + 1 >= argc) {
          return false;
        }
        std::uint64_t value = 0;
        const std::string_view text = argv[++i];
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
          return false;
        }
        options.dropAt = value;
      } else if (arg == "--dump-snapshot") {
        if (i + 1 >= argc) {
          return false;
        }
        options.dumpSnapshot = argv[++i];
      } else if (arg == "--replay-snapshot") {
        if (i + 1 >= argc) {
          return false;
        }
        options.replaySnapshot = argv[++i];
      } else if (arg == "--help" || arg == "-h") {
        options.help = true;
      } else {
        return false;
      }
    }
    // dump 与 replay 语义互斥：一次运行不可能既产生又消费同一帧。
    return options.dumpSnapshot.empty() || options.replaySnapshot.empty();
  }

  // replay 路径只依赖快照与后端：不建 SceneTree / EventBus / MainLoop / ObjectDB。
  [[nodiscard]] int runReplay(yr::render::VulkanBackend& backend, const Options& options) {
    std::vector<std::uint8_t> bytes;
    if (!readBytes(options.replaySnapshot, bytes)) {
      YR_LOG_ERROR("render_demo", "replay_snapshot 读取失败：" + options.replaySnapshot);
      backend.shutdown();
      return 1;
    }

    yr::render::RenderSnapshot snapshot;
    if (!yr::render::decodeSnapshot(bytes.data(), bytes.size(), snapshot)) {
      YR_LOG_ERROR("render_demo", "replay_snapshot 解码失败：" + options.replaySnapshot);
      backend.shutdown();
      return 1;
    }

    YR_LOG_INFO("render_demo", "replay_snapshot frame_index=" + std::to_string(snapshot.frame_index) +
                                   " items=" + std::to_string(snapshot.items.size()));

    std::uint64_t frames = 0;
    while (true) {
      backend.pollEvents();
      if (backend.shouldClose()) {
        break;
      }
      backend.present(snapshot);
      snapshot.uploads = {};
      snapshot.releases = {};
      ++frames;
      if (options.frames != 0 && frames >= options.frames) {
        break;
      }
    }

    const yr::render::BackendStats backendStats = backend.last_frame_stats();
    backend.shutdown();
    const std::uint32_t errors = backend.validationErrorCount();
    YR_LOG_INFO("render_demo",
                "replay frames=" + std::to_string(frames) + " items=" + std::to_string(backendStats.items) +
                    " draw_calls=" + std::to_string(backendStats.draw_calls) +
                    " triangles=" + std::to_string(backendStats.triangles) +
                    " uploads=" + std::to_string(backendStats.uploaded_assets) +
                    " releases=" + std::to_string(backendStats.released_assets) +
                    " missing=" + std::to_string(backendStats.missing_assets) + " upload_bytes=" +
                    std::to_string(backendStats.upload_bytes) + " validation_errors=" + std::to_string(errors));
    return errors == 0 ? 0 : 1;
  }

  [[nodiscard]] std::unique_ptr<yr::scene::Node> buildGltfNode(const yr::asset::GltfNode& source) {
    auto node = std::make_unique<yr::scene::Node>();
    node->setName(source.name);
    node->setTransform(source.localTransform);
    for (std::size_t index = 0; index < source.primitives.size(); ++index) {
      const yr::asset::GltfPrimitive& primitive = source.primitives[index];
      auto meshNode = std::make_unique<yr::scene::MeshInstanceNode>();
      meshNode->setName(source.name + ".primitive." + std::to_string(index));
      meshNode->setMesh(primitive.mesh);
      meshNode->setMaterial(primitive.material);
      if (!node->addChild(std::move(meshNode))) {
        return nullptr;
      }
    }
    for (const yr::asset::GltfNode& child : source.children) {
      auto childNode = buildGltfNode(child);
      if (!childNode || !node->addChild(std::move(childNode))) {
        return nullptr;
      }
    }
    return node;
  }

  void calculateBounds(yr::scene::Node& node, yr::math::Vec3& minimum, yr::math::Vec3& maximum, bool& hasVertex) {
    if (auto* meshNode = dynamic_cast<yr::scene::MeshInstanceNode*>(&node); meshNode != nullptr && meshNode->mesh()) {
      for (const yr::asset::MeshVertex& vertex : meshNode->mesh()->vertices) {
        const yr::math::Vec3 point = meshNode->globalTransform().transformPoint(vertex.position);
        if (!hasVertex) {
          minimum = point;
          maximum = point;
          hasVertex = true;
        } else {
          minimum.x = std::min(minimum.x, point.x);
          minimum.y = std::min(minimum.y, point.y);
          minimum.z = std::min(minimum.z, point.z);
          maximum.x = std::max(maximum.x, point.x);
          maximum.y = std::max(maximum.y, point.y);
          maximum.z = std::max(maximum.z, point.z);
        }
      }
    }
    for (std::size_t index = 0; index < node.childCount(); ++index) {
      if (yr::scene::Node* child = node.childAt(index); child != nullptr) {
        calculateBounds(*child, minimum, maximum, hasVertex);
      }
    }
  }

  [[nodiscard]] yr::scene::MeshInstanceNode* findFirstMeshInstance(yr::scene::Node& node) {
    if (auto* meshNode = dynamic_cast<yr::scene::MeshInstanceNode*>(&node); meshNode != nullptr) {
      return meshNode;
    }
    for (std::size_t index = 0; index < node.childCount(); ++index) {
      if (yr::scene::Node* child = node.childAt(index); child != nullptr) {
        if (yr::scene::MeshInstanceNode* meshNode = findFirstMeshInstance(*child); meshNode != nullptr) {
          return meshNode;
        }
      }
    }
    return nullptr;
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

  // backend 由本入口拥有，生命周期覆盖主循环；MainLoop 只持非拥有指针。
  yr::render::VulkanBackend backend;
  if (!backend.initialize({})) {
    return 1;
  }

  // replay 在初始化之后、建场景之前分流：证明快照 + 后端即可出画。
  if (!options.replaySnapshot.empty()) {
    return runReplay(backend, options);
  }

  // 声明顺序保证析构顺序：loop → tree → assets → backend。
  yr::asset::AssetDatabase assets(YR_ASSET_ROOT);

  std::unique_ptr<yr::scene::Node> root;
  {
    // GltfScene 仅用于构建节点树；资源缓存由 AssetDatabase 持有，节点持自己的 Ref。
    yr::asset::GltfLoadResult loaded = assets.loadGltfScene("res://models/ToyCar/ToyCar.gltf");
    if (!loaded) {
      YR_LOG_ERROR("render_demo", "ToyCar 加载失败：" + loaded.error);
      backend.shutdown();
      return 1;
    }
    root = std::make_unique<yr::scene::Node>();
    root->setName("ToyCarScene");
    for (const yr::asset::GltfNode& gltfRoot : loaded.scene.roots) {
      auto child = buildGltfNode(gltfRoot);
      if (!child || !root->addChild(std::move(child))) {
        YR_LOG_ERROR("render_demo", "ToyCar 节点构建失败");
        backend.shutdown();
        return 1;
      }
    }
  }

  yr::scene::SceneTree tree;
  yr::scene::Node* const movable = root.get();
  if (!tree.setRoot(std::move(root))) {
    YR_LOG_ERROR("render_demo", "场景构建失败");
    backend.shutdown();
    return 1;
  }
  tree.refreshTransforms();

  yr::math::Vec3 minimum{};
  yr::math::Vec3 maximum{};
  bool hasVertex = false;
  calculateBounds(*tree.root(), minimum, maximum, hasVertex);
  if (!hasVertex) {
    YR_LOG_ERROR("render_demo", "ToyCar 没有可用顶点");
    backend.shutdown();
    return 1;
  }
  const yr::math::Vec3 center = (minimum + maximum) * 0.5F;
  const float radius = std::max((maximum - minimum).length() * 0.5F, 0.001F);
  const yr::math::Vec3 eye = center + yr::math::Vec3{1.2F * radius, 0.8F * radius, 1.8F * radius};

  // 相机由 ToyCar 的世界空间 AABB 定位，MainLoop 只转交。
  constexpr float kFovYRadians = 60.0F * std::numbers::pi_v<float> / 180.0F;
  yr::render::CameraPod camera;
  camera.eye[0] = eye.x;
  camera.eye[1] = eye.y;
  camera.eye[2] = eye.z;
  camera.proj = toTransform(
      yr::math::Mat4::perspective(kFovYRadians, 1280.0F / 720.0F, std::max(0.001F, radius * 0.01F), radius * 100.0F));
  camera.view = toTransform(yr::math::Mat4::lookAt(eye, center, yr::math::Vec3{0.0F, 1.0F, 0.0F}));

  yr::evt::EventBus events;
  DemoLoop loop(tree, events, backend, *movable, options.dumpSnapshot);
  loop.setBackend(&backend);      // 阶段 11 调 present
  loop.setAssetDatabase(&assets); // 阶段 12 调 collectGarbage
  loop.setCamera(camera);
  yr::engine::Clock clock;

  // soak 时不刷周期报告，退出时统一打一帧摘要；窗口仍是默认的最近 4096 帧。
  loop.stats().setReportInterval(0);

  std::uint64_t ticks = 0;
  std::uint64_t releasedAssets = 0;
  bool dropQueued = false;
  while (!loop.shouldQuit()) {
    loop.tick(clock.nextDelta());
    releasedAssets += backend.last_frame_stats().released_assets;
    ++ticks;
    if (!dropQueued && options.dropAt != 0 && ticks == options.dropAt) {
      if (yr::scene::MeshInstanceNode* meshNode = findFirstMeshInstance(*tree.root()); meshNode != nullptr) {
        meshNode->queueFree();
        dropQueued = true;
        YR_LOG_INFO("render_demo", "queue_free MeshInstanceNode after tick " + std::to_string(ticks));
      }
    }
    if (options.frames != 0 && ticks >= options.frames) {
      break;
    }
  }

  // last_frame_stats / validation 都必须在 shutdown 前/后按各自契约取：前者会被 shutdown 清零。
  yr::render::BackendStats backendStats = backend.last_frame_stats();
  // BackendStats is per-frame; retain release diagnostics across the run for the summary.
  backendStats.released_assets = releasedAssets;
  const yr::engine::FrameStats& stats = loop.stats();

  // 循环结束后先释放场景，再让 DB 收掉仅剩自己持有的资源；DemoLoop 析构不访问 movable。
  (void)tree.clearRoot();
  const yr::asset::GcStats finalGc = assets.collectGarbage();
  const yr::asset::AssetStats finalAssets = assets.stats();
  (void)finalGc; // 累计 evicted 已包含本次清理，摘要只输出 AssetStats 口径。

  backend.shutdown();

  const std::uint32_t errors = backend.validationErrorCount();
  std::string summary = "frames=" + std::to_string(loop.frameCount());
  summary += " | stats_window=last " + std::to_string(stats.windowCount()) + "/" + std::to_string(stats.windowSize()) +
             " frames";
  summary += " total p50=" + formatDuration(stats.totalP50()) + " p95=" + formatDuration(stats.totalP95());
  summary += " extract_render p50=" + formatDuration(stats.phaseP50(yr::engine::Phase::kExtractRender)) +
             " p95=" + formatDuration(stats.phaseP95(yr::engine::Phase::kExtractRender));
  summary += " present p50=" + formatDuration(stats.phaseP50(yr::engine::Phase::kPresent)) +
             " p95=" + formatDuration(stats.phaseP95(yr::engine::Phase::kPresent));
  summary += " | items=" + std::to_string(backendStats.items) +
             " draw_calls=" + std::to_string(backendStats.draw_calls) +
             " triangles=" + std::to_string(backendStats.triangles) +
             " uploads=" + std::to_string(backendStats.uploaded_assets) +
             " releases=" + std::to_string(backendStats.released_assets) +
             " missing=" + std::to_string(backendStats.missing_assets) +
             " upload_bytes=" + std::to_string(backendStats.upload_bytes);
  summary +=
      " resident=" + std::to_string(finalAssets.resident_count) +
      " cpu_bytes=" + std::to_string(finalAssets.cpu_bytes) + " gc_evicted=" + std::to_string(finalAssets.evicted) +
      " asset_loads=" + std::to_string(finalAssets.loads) + " asset_hits=" + std::to_string(finalAssets.cache_hits);
  summary += " validation_errors=" + std::to_string(errors);
  YR_LOG_INFO("render_demo", summary);
  return errors == 0 && !loop.dumpFailed() ? 0 : 1;
}
