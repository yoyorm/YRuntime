// render_demo 正式运行入口：M7b 由 MainLoop 驱动，Vulkan 后端按快照中的 DrawItem 逐个绘制。

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <system_error>

#include <yr/core/log.h>
#include <yr/engine/clock.h>
#include <yr/engine/main_loop.h>
#include <yr/event/event_bus.h>
#include <yr/math/mat4.h>
#include <yr/math/vec3.h>
#include <yr/render/snapshot.h>
#include <yr/render/vk/vulkan_backend.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

namespace {

  // host 驱动 MainLoop：阶段 1 泵窗口事件，窗口关闭转成 requestQuit。
  class DemoLoop final : public yr::engine::MainLoop {
  public:
    DemoLoop(yr::scene::SceneTree& tree, yr::evt::EventBus& events, yr::render::VulkanBackend& backend)
        : MainLoop(tree, events), backend_(backend) {}

  protected:
    void pollInput() override {
      backend_.pollEvents();
      if (backend_.shouldClose()) {
        requestQuit();
      }
    }

  private:
    yr::render::VulkanBackend& backend_;
  };

  struct Options {
    std::uint64_t frames = 0; // 0 = 直到窗口关闭
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

  // backend 由本入口拥有，生命周期覆盖主循环；MainLoop 只持非拥有指针。
  yr::render::VulkanBackend backend;
  if (!backend.initialize({})) {
    return 1;
  }

  // 场景：root 下 3 个子节点沿 X 轴排开，便于验证逐 item 的 world 变换。
  yr::scene::SceneTree tree;
  auto root = std::make_unique<yr::scene::Node>();
  auto left = std::make_unique<yr::scene::Node>();
  auto middle = std::make_unique<yr::scene::Node>();
  auto right = std::make_unique<yr::scene::Node>();
  left->setPosition(yr::math::Vec3{-1.2F, 0.0F, 0.0F});
  middle->setPosition(yr::math::Vec3{0.0F, 1.2F, 0.0F});
  right->setPosition(yr::math::Vec3{1.2F, 0.0F, 0.0F});
  if (!(root->addChild(std::move(left)) && root->addChild(std::move(middle)) && root->addChild(std::move(right))) ||
      !tree.setRoot(std::move(root))) {
    YR_LOG_ERROR("render_demo", "场景构建失败");
    backend.shutdown();
    return 1;
  }

  // 相机由 host 计算，MainLoop 只转交；1280x720 与默认窗口一致。
  constexpr float kFovYRadians = 60.0F * std::numbers::pi_v<float> / 180.0F;
  yr::render::CameraPod camera;
  camera.proj = toTransform(yr::math::Mat4::perspective(kFovYRadians, 1280.0F / 720.0F, 0.1F, 100.0F));
  camera.view = toTransform(yr::math::Mat4::lookAt(yr::math::Vec3{0.0F, 0.0F, 4.0F}, yr::math::Vec3{0.0F, 0.0F, 0.0F},
                                                   yr::math::Vec3{0.0F, 1.0F, 0.0F}));

  yr::evt::EventBus events;
  DemoLoop loop(tree, events, backend);
  loop.setBackend(&backend); // 阶段 11 调 present
  loop.setCamera(camera);
  yr::engine::Clock clock;

  std::uint64_t ticks = 0;
  while (!loop.shouldQuit()) {
    loop.tick(clock.nextDelta());
    ++ticks;
    if (options.frames != 0 && ticks >= options.frames) {
      break;
    }
  }
  backend.shutdown();

  const std::uint32_t errors = backend.validationErrorCount();
  YR_LOG_INFO("render_demo",
              "frames=" + std::to_string(loop.frameCount()) + " validation_errors=" + std::to_string(errors));
  return errors == 0 ? 0 : 1;
}