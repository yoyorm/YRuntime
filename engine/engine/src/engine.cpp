#include <yr/engine/engine.h>

namespace yr::engine {

  Engine::Engine() noexcept : mainLoop_(sceneTree_, eventBus_) {}

  void Engine::run(std::uint64_t maxFrames) {
    for (std::uint64_t frame = 0; frame < maxFrames; ++frame) {
      mainLoop_.tick(clock_.nextDelta());
      if (mainLoop_.shouldQuit()) {
        break;
      }
    }
  }

} // namespace yr::engine