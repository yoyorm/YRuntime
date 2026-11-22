#pragma once

#include <array>
#include <cstdint>
#include <yr/core/timer.h>
#include <yr/engine/frame_stats.h>
#include <yr/engine/phase.h>
#include <yr/engine/render_extract.h>
#include <yr/render/backend.h>
#include <yr/render/snapshot.h>

namespace yr::scene {
  class SceneTree;
}
namespace yr::evt {
  class EventBus;
}
namespace yr::asset {
  class AssetDatabase;
}
namespace yr::input {
  class InputSystem;
}

namespace yr::engine {

  // 帧阶段序列
  class MainLoop {
  public:
    MainLoop(scene::SceneTree& tree, evt::EventBus& events) noexcept;
    virtual ~MainLoop() = default;

    MainLoop(const MainLoop&) = delete;
    MainLoop& operator=(const MainLoop&) = delete;
    MainLoop(MainLoop&&) = delete;
    MainLoop& operator=(MainLoop&&) = delete;

    void tick(double realDelta);

    // 已执行的 tick 数。
    [[nodiscard]] std::uint64_t frameCount() const noexcept { return frameIndex_; }

    // 最近一次已执行帧的序号
    [[nodiscard]] std::uint64_t frameIndex() const noexcept { return frameIndex_ == 0 ? 0 : frameIndex_ - 1; }

    // 本帧 realDelta
    [[nodiscard]] double delta() const noexcept { return realDelta_; }

    [[nodiscard]] bool shouldQuit() const noexcept { return quitRequested_; }
    void requestQuit() noexcept { quitRequested_ = true; }

    // timeScale 只影响 gameDelta_
    [[nodiscard]] double timeScale() const noexcept { return timeScale_; }
    void setTimeScale(double scale) noexcept { timeScale_ = scale > 0.0 ? scale : 0.0; }

    [[nodiscard]] bool isPaused() const noexcept { return paused_; }
    void setPaused(bool paused) noexcept { paused_ = paused; }

    // 固定物理步长（秒）。
    [[nodiscard]] double physicsStep() const noexcept { return kPhysicsStep; }
    [[nodiscard]] double accumulator() const noexcept { return accumulator_; }

    [[nodiscard]] scene::SceneTree& tree() noexcept { return sceneTree_; }
    [[nodiscard]] evt::EventBus& events() noexcept { return eventBus_; }

    void setBackend(render::RendererBackend* backend) noexcept {
      if (backend_ != backend) {
        renderExtract_.reset();
        backend_ = backend;
      }
    }

    void setAssetDatabase(asset::AssetDatabase* database) noexcept { assetDatabase_ = database; }

    void setInputSystem(input::InputSystem* system) noexcept { inputSystem_ = system; }

    // 当前帧快照，下一次 tick 被覆写
    [[nodiscard]] render::RenderSnapshot& snapshot() noexcept { return snapshot_; }
    [[nodiscard]] const render::RenderSnapshot& snapshot() const noexcept { return snapshot_; }

    // host 计算的相机；默认 identity。
    [[nodiscard]] const render::CameraPod& camera() const noexcept { return camera_; }
    void setCamera(const render::CameraPod& camera) noexcept { camera_ = camera; }

    // host 计算的方向光阴影；默认 disabled。
    [[nodiscard]] const render::ShadowPod& shadow() const noexcept { return shadow_; }
    void setShadow(const render::ShadowPod& shadow) noexcept { shadow_ = shadow; }

    // 组合持有：记录本帧各阶段耗时、环形窗口与周期报告。构造后、tick 前可配置报告频率 /
    // CSV 路径 / 静默（setReportInterval(0)）。
    [[nodiscard]] FrameStats& stats() noexcept { return stats_; }
    [[nodiscard]] const FrameStats& stats() const noexcept { return stats_; }

  protected:
    virtual void beginFrame();
    virtual void pollInput();
    virtual void flushMessages();
    virtual void flushEvents();
    virtual void pumpAssets();
    virtual void prePhysics();
    virtual void physicsSteps();
    virtual void processPhase();
    virtual void postProcess();
    virtual void applyStructure();
    virtual void extractRender();
    virtual void present();
    virtual void collectGarbage();
    virtual void endFrame();
    virtual void onPhysicsStep(double physicsDelta);

  private:
    [[nodiscard]] double effectiveTimeScale() const noexcept { return paused_ ? 0.0 : timeScale_; }

    // 固定步长 1/60
    static constexpr double kPhysicsStep = 1.0 / 60.0;
    static constexpr int kMaxPhysicsSteps = 8;

    scene::SceneTree& sceneTree_;
    evt::EventBus& eventBus_;
    render::RendererBackend* backend_ = nullptr;

    std::uint64_t frameIndex_ = 0;
    double realDelta_ = 0.0;
    double gameDelta_ = 0.0;
    double accumulator_ = 0.0;
    double timeScale_ = 1.0;
    int physicsStepsFrame_ = 0;

    bool paused_ = false;

    bool spiralReported_ = false;
    bool quitRequested_ = false;

    asset::AssetDatabase* assetDatabase_ = nullptr;
    input::InputSystem* inputSystem_ = nullptr;
    render::RenderSnapshot snapshot_;
    render::CameraPod camera_{};
    render::ShadowPod shadow_{};
    RenderExtractSystem renderExtract_;

    FrameStats stats_;
    core::ScopeTimer frameTimer_;
    core::ScopeTimer phaseTimer_;
    std::array<core::Duration, kPhaseCount> phaseDurations_{};
  };

} // namespace yr::engine
