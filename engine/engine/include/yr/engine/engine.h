#pragma once

#include <cstdint>

#include <yr/engine/clock.h>
#include <yr/engine/main_loop.h>
#include <yr/event/event_bus.h>
#include <yr/scene/scene_tree.h>

namespace yr::engine {

  // Engine：headless 顶层装配。组合拥有 SceneTree / EventBus / Clock / MainLoop 并驱动 while loop；
  // 帧阶段顺序唯一定义在 MainLoop，Engine 不复制编排。
  // 声明顺序 sceneTree_ → eventBus_ → clock_ → mainLoop_，析构逆序保证 mainLoop_ 最先析构。
  class Engine {
  public:
    Engine() noexcept;
    ~Engine() = default;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    // 运行最多 maxFrames 帧：每帧 tick(clock_.nextDelta())，达到上限或 shouldQuit() 后退出。
    // 使用真实 Clock，不作 delta 确定性保证；确定性测试直接用 MainLoop::tick(fixedDelta)。
    void run(std::uint64_t maxFrames);

    [[nodiscard]] scene::SceneTree& sceneTree() noexcept { return sceneTree_; }
    [[nodiscard]] evt::EventBus& events() noexcept { return eventBus_; }
    [[nodiscard]] MainLoop& mainLoop() noexcept { return mainLoop_; }

  private:
    scene::SceneTree sceneTree_;
    evt::EventBus eventBus_;
    Clock clock_;
    MainLoop mainLoop_;
  };

} // namespace yr::engine