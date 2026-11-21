#pragma once

#include <cstdint>

#include <yr/engine/clock.h>
#include <yr/engine/main_loop.h>
#include <yr/event/event_bus.h>
#include <yr/scene/scene_tree.h>

namespace yr::render {
  class RendererBackend;
}

namespace yr::engine {

  class Engine {
  public:
    Engine() noexcept;
    ~Engine() = default;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    void run(std::uint64_t maxFrames);
    void setBackend(render::RendererBackend* backend) noexcept;

    [[nodiscard]] scene::SceneTree& sceneTree() noexcept { return sceneTree_; }
    [[nodiscard]] evt::EventBus& events() noexcept { return eventBus_; }
    [[nodiscard]] MainLoop& mainLoop() noexcept { return mainLoop_; }

  private:
    scene::SceneTree sceneTree_;
    evt::EventBus eventBus_;
    Clock clock_;
    MainLoop mainLoop_; // 最先析构
  };

} // namespace yr::engine