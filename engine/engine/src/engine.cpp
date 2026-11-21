#include <yr/engine/engine.h>

#include <yr/core/thread_check.h>

namespace yr::engine {

  Engine::Engine() noexcept : mainLoop_(sceneTree_, eventBus_) {
    core::markMainThread();
  }

  void Engine::setBackend(render::RendererBackend* backend) noexcept {
    mainLoop_.setBackend(backend);
  }

  void Engine::run(std::uint64_t maxFrames) {
    for (std::uint64_t frame = 0; frame < maxFrames; ++frame) {
      mainLoop_.tick(clock_.nextDelta());
      if (mainLoop_.shouldQuit()) {
        break;
      }
    }
  }

} // namespace yr::engine