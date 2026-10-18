#pragma once

#include <array>
#include <cstdint>
#include <yr/core/timer.h>
#include <yr/engine/frame_stats.h>
#include <yr/engine/phase.h>

namespace yr::scene {
  class SceneTree;
}
namespace yr::evt {
  class EventBus;
}

namespace yr::engine {

  // MainLoop：帧阶段序列的唯一权威实现，一次 tick = 一次 0→13。
  //
  // 引用非拥有：SceneTree / EventBus 由装配者拥有，必须比 MainLoop 活得更久；不提供默认
  // globalEventBus 路径。只负责一帧内顺序，quit 标志由外层驱动，tick() 不读取它。
  //
  // 时间：gameDelta_ = realDelta_ * effectiveTimeScale()（paused 独立于 timeScale，恢复保留原
  // scale）；固定步长累加器消费 gameDelta_，阶段 7 的 process 也收 gameDelta_。
  // 阶段方法为 protected virtual，便于测试探针记录顺序与后续扩展。完整阶段表见 doc/layers/engine.md。
  class MainLoop {
  public:
    MainLoop(scene::SceneTree& tree, evt::EventBus& events) noexcept;
    virtual ~MainLoop() = default;

    MainLoop(const MainLoop&) = delete;
    MainLoop& operator=(const MainLoop&) = delete;
    MainLoop(MainLoop&&) = delete;
    MainLoop& operator=(MainLoop&&) = delete;

    // 执行 0→13 一次；realDelta 是本帧墙钟秒数，由调用方注入（测试用确定性值，headless
    // 用 Clock::nextDelta()）。不检查 shouldQuit（退出决策在外层）。
    void tick(double realDelta);

    // 已执行的 tick 数。
    [[nodiscard]] std::uint64_t frameCount() const noexcept { return frameIndex_; }

    // 最近一次已执行帧的 0-based 序号；未 tick 时为 0。
    [[nodiscard]] std::uint64_t frameIndex() const noexcept { return frameIndex_ == 0 ? 0 : frameIndex_ - 1; }

    [[nodiscard]] bool shouldQuit() const noexcept { return quitRequested_; }
    void requestQuit() noexcept { quitRequested_ = true; }

    // timeScale 只影响 gameDelta_（process dt 与物理累积）；负值 clamp 到 0。
    [[nodiscard]] double timeScale() const noexcept { return timeScale_; }
    void setTimeScale(double scale) noexcept { timeScale_ = scale > 0.0 ? scale : 0.0; }

    // paused 独立存储：effectiveTimeScale = paused ? 0 : timeScale，恢复时保留原 timeScale。
    [[nodiscard]] bool isPaused() const noexcept { return paused_; }
    void setPaused(bool paused) noexcept { paused_ = paused; }

    // 固定物理步长（秒）。
    [[nodiscard]] double physicsStep() const noexcept { return kPhysicsStep; }
    // 当前未消费的物理累积（诊断用），始终 < physicsStep（spiral 截断后）。
    [[nodiscard]] double accumulator() const noexcept { return accumulator_; }

    [[nodiscard]] scene::SceneTree& tree() noexcept { return sceneTree_; }
    [[nodiscard]] evt::EventBus& events() noexcept { return eventBus_; }

    // 组合持有：记录本帧各阶段耗时、环形窗口与周期报告。构造后、tick 前可配置报告频率 /
    // CSV 路径 / 静默（setReportInterval(0)）。
    [[nodiscard]] FrameStats& stats() noexcept { return stats_; }
    [[nodiscard]] const FrameStats& stats() const noexcept { return stats_; }

  protected:
    // 默认空实现；接线时保持签名不变。
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

    // 阶段 6 内循环的每一步调用一次；默认 no-op，physics 后端与测试探针覆写。
    virtual void onPhysicsStep(double physicsDelta);

  private:
    [[nodiscard]] double effectiveTimeScale() const noexcept { return paused_ ? 0.0 : timeScale_; }

    // 固定步长 1/60，spiral 上限对齐 Godot 默认 8 步/帧。
    static constexpr double kPhysicsStep = 1.0 / 60.0;
    static constexpr int kMaxPhysicsSteps = 8;

    scene::SceneTree& sceneTree_;
    evt::EventBus& eventBus_;
    // beginFrame 递增，故等于已开始的帧数（下一帧的 0-based 序号）。
    std::uint64_t frameIndex_ = 0;
    // 本帧 realDelta，在 beginFrame 前暂存；gameDelta_ 由它乘 effectiveTimeScale 得到。
    double realDelta_ = 0.0;
    // 本帧喂给阶段 7 的缩放 delta。
    double gameDelta_ = 0.0;
    // 固定步长累加器，消费 gameDelta_；始终保留亚步余量。
    double accumulator_ = 0.0;
    double timeScale_ = 1.0;
    bool paused_ = false;
    // spiral 截断只报告一次，避免卡顿时每帧刷屏。
    bool spiralReported_ = false;
    bool quitRequested_ = false;

    // 帧统计：tick 全程计时 + 每阶段计时，endFrame 汇总入环。
    FrameStats stats_;
    core::ScopeTimer frameTimer_;
    core::ScopeTimer phaseTimer_;
    std::array<core::Duration, kPhaseCount> phaseDurations_{};
    // 本帧 physics 步数，由 physicsSteps() 写入，随帧统计一起上报。
    int physicsStepsFrame_ = 0;
  };

} // namespace yr::engine