// render_demo 正式运行入口：MainLoop 驱动、键盘移动 Node、快照 dump / replay，
// M9.2b 增加外部 glTF、mesh/texture 流式加载与逐帧 trace。

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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
#include <yr/scene/light_node.h>
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

  [[nodiscard]] double durationMs(yr::core::Duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  }

  [[nodiscard]] double elapsedMs(yr::core::TimePoint start, yr::core::TimePoint end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
  }

  // summary / 日志统一两位小数，避免 to_string 输出长尾。
  [[nodiscard]] std::string formatMs(double value) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return std::string(buffer);
  }

  // Linux 下 VmRSS 单位为 kB；其他平台或读取失败返回 0。
  [[nodiscard]] std::uint64_t readRssKb() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
      constexpr std::string_view kPrefix = "VmRSS:";
      if (line.rfind(kPrefix.data(), 0) != 0) {
        continue;
      }
      std::string_view digits(line);
      digits.remove_prefix(kPrefix.size());
      while (!digits.empty() && (digits.front() == ' ' || digits.front() == '\t')) {
        digits.remove_prefix(1);
      }
      std::uint64_t value = 0;
      const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
      return result.ec == std::errc{} ? value : 0;
    }
#endif
    return 0;
  }

  // host 驱动 MainLoop：阶段 1 泵窗口事件，WASD 小幅平移场景根；关闭窗口转成 requestQuit。
  class DemoLoop final : public yr::engine::MainLoop {
  public:
    DemoLoop(yr::scene::SceneTree& tree, yr::evt::EventBus& events, yr::render::VulkanBackend& backend,
             yr::scene::Node& movable, std::string dumpPath)
        : MainLoop(tree, events), backend_(backend), movable_(movable), dumpPath_(std::move(dumpPath)) {}

    [[nodiscard]] bool dumpFailed() const noexcept { return dumpFailed_; }

    [[nodiscard]] yr::core::Duration lastPump() const noexcept { return lastPump_; }
    [[nodiscard]] yr::core::Duration lastExtract() const noexcept { return lastExtract_; }
    [[nodiscard]] yr::core::Duration lastPresent() const noexcept { return lastPresent_; }
    [[nodiscard]] yr::core::Duration lastGc() const noexcept { return lastGc_; }

  protected:
    void pumpAssets() override {
      yr::core::ScopeTimer timer;
      MainLoop::pumpAssets();
      lastPump_ = timer.elapsed();
    }

    void extractRender() override {
      yr::core::ScopeTimer timer;
      MainLoop::extractRender();
      lastExtract_ = timer.elapsed();
    }

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
      yr::core::ScopeTimer timer;
      MainLoop::present();
      lastPresent_ = timer.elapsed();
    }

    void collectGarbage() override {
      yr::core::ScopeTimer timer;
      MainLoop::collectGarbage();
      lastGc_ = timer.elapsed();
    }

  private:
    yr::render::VulkanBackend& backend_;
    // 非拥有：树由 main 持有，且比 loop 活得更久。
    yr::scene::Node& movable_;
    std::string dumpPath_;
    bool dumped_ = false;
    bool dumpFailed_ = false;
    yr::core::Duration lastPump_{};
    yr::core::Duration lastExtract_{};
    yr::core::Duration lastPresent_{};
    yr::core::Duration lastGc_{};
  };

  struct Options {
    std::uint64_t frames = 0; // 0 = 直到窗口关闭
    std::uint64_t dropAt = 0;
    std::string dumpSnapshot;
    std::string replaySnapshot;
    std::string gltfPath = "res://models/ToyCar/ToyCar.gltf";
    std::string assetRoot = YR_ASSET_ROOT;
    std::string tracePath;
    bool asyncTextures = false;
    bool streamAssets = false;
    bool skipTextures = false;
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
                 "          [--gltf RES_PATH] [--asset-root PATH] [--stream-assets] [--skip-textures]\n"
                 "          [--async-textures] [--trace PATH]\n"
                 "  --gltf RES_PATH          glTF scene to load (default res://models/ToyCar/ToyCar.gltf)\n"
                 "  --asset-root PATH        filesystem root for res:// (default YR_ASSET_ROOT)\n"
                 "  --stream-assets          defer both meshes and textures\n"
                 "  --skip-textures          create no textures (geometry stress test)\n"
                 "  --async-textures         stream glTF textures through AssetDatabase/LoaderWorker\n"
                 "  --trace PATH             write a per-frame CSV trace\n"
                 "  --dump-snapshot PATH     dump the first presented RenderSnapshot\n"
                 "  --drop-at N              queue the first MeshInstanceNode for deletion after tick N\n"
                 "  --replay-snapshot PATH   replay a dumped snapshot without a SceneTree\n",
                 program);
  }

  // 手写参数解析：只支持少量运行开关，不引入 CLI 库。
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
      } else if (arg == "--gltf") {
        if (i + 1 >= argc) {
          return false;
        }
        options.gltfPath = argv[++i];
      } else if (arg == "--asset-root") {
        if (i + 1 >= argc) {
          return false;
        }
        options.assetRoot = argv[++i];
      } else if (arg == "--trace") {
        if (i + 1 >= argc) {
          return false;
        }
        options.tracePath = argv[++i];
      } else if (arg == "--stream-assets") {
        options.streamAssets = true; // 幂等：重复出现不改变结果
      } else if (arg == "--skip-textures") {
        options.skipTextures = true; // 幂等：重复出现不改变结果
      } else if (arg == "--async-textures") {
        options.asyncTextures = true; // 幂等：重复出现不改变结果
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

  [[nodiscard]] std::size_t countGltfNodes(const yr::asset::GltfNode& node) {
    std::size_t count = 1;
    for (const yr::asset::GltfNode& child : node.children) {
      count += countGltfNodes(child);
    }
    return count;
  }

  [[nodiscard]] std::size_t countGltfPrimitives(const yr::asset::GltfNode& node) {
    std::size_t count = node.primitives.size();
    for (const yr::asset::GltfNode& child : node.children) {
      count += countGltfPrimitives(child);
    }
    return count;
  }

  void expandBounds(const yr::math::Vec3& point, yr::math::Vec3& minimum, yr::math::Vec3& maximum, bool& hasVertex) {
    if (!hasVertex) {
      minimum = point;
      maximum = point;
      hasVertex = true;
      return;
    }
    minimum.x = std::min(minimum.x, point.x);
    minimum.y = std::min(minimum.y, point.y);
    minimum.z = std::min(minimum.z, point.z);
    maximum.x = std::max(maximum.x, point.x);
    maximum.y = std::max(maximum.y, point.y);
    maximum.z = std::max(maximum.z, point.z);
  }

  void calculateBounds(yr::scene::Node& node, yr::math::Vec3& minimum, yr::math::Vec3& maximum, bool& hasVertex) {
    if (auto* meshNode = dynamic_cast<yr::scene::MeshInstanceNode*>(&node); meshNode != nullptr && meshNode->mesh()) {
      const yr::asset::Mesh& mesh = *meshNode->mesh();
      if (mesh.hasBounds) {
        // deferMeshes 时顶点为空，只能把本地 AABB 的 8 个角点变换到 world。
        const yr::math::Mat4 world = meshNode->globalTransform();
        for (int corner = 0; corner < 8; ++corner) {
          const yr::math::Vec3 local{(corner & 1) != 0 ? mesh.boundsMax.x : mesh.boundsMin.x,
                                     (corner & 2) != 0 ? mesh.boundsMax.y : mesh.boundsMin.y,
                                     (corner & 4) != 0 ? mesh.boundsMax.z : mesh.boundsMin.z};
          expandBounds(world.transformPoint(local), minimum, maximum, hasVertex);
        }
      } else {
        for (const yr::asset::MeshVertex& vertex : mesh.vertices) {
          expandBounds(meshNode->globalTransform().transformPoint(vertex.position), minimum, maximum, hasVertex);
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

  void addLight(yr::scene::Node& root, yr::scene::LightType type, const yr::math::Vec3& color, float intensity,
                float range, const yr::math::Vec3& position, const yr::math::Vec3& direction) {
    auto light = std::make_unique<yr::scene::LightNode>();
    light->setType(type);
    light->setColor(color);
    light->setIntensity(intensity);
    light->setRange(range);
    light->setPosition(position);
    light->setDirection(direction);
    if (!root.addChild(std::move(light))) {
      YR_LOG_ERROR("render_demo", "灯光添加失败");
    }
  }

  // 1 方向光 + 2 点光；位置按场景 AABB 半径缩放。
  void addDemoLights(yr::scene::Node& root, const yr::math::Vec3& center, float radius) {
    addLight(root, yr::scene::LightType::kDirectional, yr::math::Vec3{1.0F, 0.95F, 0.85F}, 2.0F, 0.0F, center,
             yr::math::Vec3{0.4F, -0.8F, -0.5F});
    addLight(root, yr::scene::LightType::kPoint, yr::math::Vec3{1.0F, 0.55F, 0.25F}, 8.0F, radius * 4.0F,
             center + yr::math::Vec3{1.2F * radius, 0.8F * radius, 0.6F * radius}, yr::math::Vec3{0.0F, 0.0F, -1.0F});
    addLight(root, yr::scene::LightType::kPoint, yr::math::Vec3{0.3F, 0.5F, 1.0F}, 6.0F, radius * 4.0F,
             center + yr::math::Vec3{-1.2F * radius, 0.6F * radius, -0.8F * radius}, yr::math::Vec3{0.0F, 0.0F, -1.0F});
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
  std::error_code rootError;
  if (!std::filesystem::is_directory(options.assetRoot, rootError)) {
    YR_LOG_ERROR("render_demo", "asset root 不是目录：" + options.assetRoot);
    printUsage(stderr, argv[0]);
    backend.shutdown();
    return 1;
  }

  yr::asset::AssetDatabase assets(options.assetRoot);

  const yr::core::TimePoint loadStart = std::chrono::steady_clock::now();
  std::unique_ptr<yr::scene::Node> root;
  std::vector<yr::asset::AssetUID> pendingTextureUids;
  double sceneLoadMs = 0.0;
  {
    yr::asset::GltfLoadOptions loadOptions;
    loadOptions.deferTextures = options.asyncTextures || options.streamAssets;
    loadOptions.deferMeshes = options.streamAssets;
    loadOptions.skipTextures = options.skipTextures;

    // GltfScene 仅用于构建节点树；资源缓存由 AssetDatabase 持有，节点持自己的 Ref。
    yr::asset::GltfLoadResult loaded = assets.loadGltfScene(options.gltfPath, loadOptions);
    sceneLoadMs = elapsedMs(loadStart, std::chrono::steady_clock::now());
    if (!loaded) {
      YR_LOG_ERROR("render_demo", options.gltfPath + " 加载失败：" + loaded.error);
      backend.shutdown();
      return 1;
    }

    pendingTextureUids.reserve(loaded.pendingTextures.size());
    for (const yr::asset::GltfTextureRequest& request : loaded.pendingTextures) {
      pendingTextureUids.push_back(request.uid);
    }

    std::size_t nodeCount = 0;
    std::size_t primitiveCount = 0;
    for (const yr::asset::GltfNode& gltfRoot : loaded.scene.roots) {
      nodeCount += countGltfNodes(gltfRoot);
      primitiveCount += countGltfPrimitives(gltfRoot);
    }
    YR_LOG_INFO("render_demo", "scene_loaded path=" + options.gltfPath + " load_ms=" + formatMs(sceneLoadMs) +
                                   " nodes=" + std::to_string(nodeCount) +
                                   " primitives=" + std::to_string(primitiveCount));

    root = std::make_unique<yr::scene::Node>();
    root->setName("GltfScene");
    for (const yr::asset::GltfNode& gltfRoot : loaded.scene.roots) {
      auto child = buildGltfNode(gltfRoot);
      if (!child || !root->addChild(std::move(child))) {
        YR_LOG_ERROR("render_demo", options.gltfPath + " 节点构建失败");
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
    YR_LOG_ERROR("render_demo", "场景没有可用顶点或 bounds");
    backend.shutdown();
    return 1;
  }
  const yr::math::Vec3 center = (minimum + maximum) * 0.5F;
  const float radius = std::max((maximum - minimum).length() * 0.5F, 0.001F);
  // 灯光只用于 extract，不参与 bounds；位置基于场景 AABB。
  addDemoLights(*tree.root(), center, radius);
  tree.refreshTransforms();
  const yr::math::Vec3 eye = center + yr::math::Vec3{1.2F * radius, 0.8F * radius, 1.8F * radius};

  // 相机由场景世界空间 AABB 定位，MainLoop 只转交。
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

  std::ofstream trace;
  if (!options.tracePath.empty()) {
    trace.open(options.tracePath, std::ios::trunc);
    if (!trace) {
      YR_LOG_ERROR("render_demo", "trace 打开失败：" + options.tracePath);
      backend.shutdown();
      return 1;
    }
    trace << "frame,wall_ms,tick_ms,pump_ms,extract_ms,present_ms,gc_ms,items,uploads,upload_bytes,missing,"
             "mesh_pending,texture_pending,resident,cpu_bytes,rss_kb\n";
  }

  std::uint64_t ticks = 0;
  std::uint64_t releasedAssets = 0;
  bool dropQueued = false;
  std::uint64_t firstItemFrame = 0;
  double firstItemMs = 0.0;
  std::uint64_t readyFrame = 0;
  double readyMs = 0.0;
  while (!loop.shouldQuit()) {
    yr::core::ScopeTimer tickTimer;
    loop.tick(clock.nextDelta());
    const yr::core::Duration tickDuration = tickTimer.elapsed();
    ++ticks;

    const yr::render::BackendStats frameStats = backend.last_frame_stats();
    releasedAssets += frameStats.released_assets;
    const yr::asset::AssetStats assetStats = assets.stats();
    std::size_t texturePending = 0;
    for (const yr::asset::AssetUID uid : pendingTextureUids) {
      if (assets.state(uid) == yr::asset::LoadState::kLoading) {
        ++texturePending;
      }
    }
    const double wallMs = elapsedMs(loadStart, std::chrono::steady_clock::now());

    if (firstItemFrame == 0 && frameStats.items > 0) {
      firstItemFrame = loop.frameCount();
      firstItemMs = wallMs;
    }
    // kFailed 的纹理不再等：ready 只要求没有 kLoading、没有待建 mesh。
    if (readyFrame == 0 && assetStats.pending_meshes == 0 && texturePending == 0) {
      readyFrame = loop.frameCount();
      readyMs = wallMs;
    }

    if (trace) {
      trace << loop.frameCount() << ',' << wallMs << ',' << durationMs(tickDuration) << ','
            << durationMs(loop.lastPump()) << ',' << durationMs(loop.lastExtract()) << ','
            << durationMs(loop.lastPresent()) << ',' << durationMs(loop.lastGc()) << ',' << frameStats.items << ','
            << frameStats.uploaded_assets << ',' << frameStats.upload_bytes << ',' << frameStats.missing_assets << ','
            << assetStats.pending_meshes << ',' << texturePending << ',' << assetStats.resident_count << ','
            << assetStats.cpu_bytes << ',' << readRssKb() << '\n';
    }

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
  if (trace) {
    trace.flush();
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
  summary += " scene_load_ms=" + formatMs(sceneLoadMs);
  summary += " first_item_frame=" + std::to_string(firstItemFrame) + " first_item_ms=" + formatMs(firstItemMs);
  summary += " ready_frame=" + std::to_string(readyFrame) + " ready_ms=" + formatMs(readyMs);
  summary += " pending_meshes=" + std::to_string(finalAssets.pending_meshes) +
             " failed_loads=" + std::to_string(finalAssets.failed_loads);
  summary += " | stats_window=last " + std::to_string(stats.windowCount()) + "/" + std::to_string(stats.windowSize()) +
             " frames";
  summary += " total p50=" + formatDuration(stats.totalP50()) + " p95=" + formatDuration(stats.totalP95());
  summary += " pump p50=" + formatDuration(stats.phaseP50(yr::engine::Phase::kPumpAssets)) +
             " p95=" + formatDuration(stats.phaseP95(yr::engine::Phase::kPumpAssets)) +
             " max=" + formatDuration(stats.phaseMax(yr::engine::Phase::kPumpAssets));
  summary += " extract_render p50=" + formatDuration(stats.phaseP50(yr::engine::Phase::kExtractRender)) +
             " p95=" + formatDuration(stats.phaseP95(yr::engine::Phase::kExtractRender));
  summary += " present p50=" + formatDuration(stats.phaseP50(yr::engine::Phase::kPresent)) +
             " p95=" + formatDuration(stats.phaseP95(yr::engine::Phase::kPresent));
  summary += " gc p50=" + formatDuration(stats.phaseP50(yr::engine::Phase::kCollectGarbage));
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
