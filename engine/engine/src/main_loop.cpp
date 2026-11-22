#include <cmath>
#include <cstddef>
#include <yr/asset/asset_database.h>
#include <yr/core/log.h>
#include <yr/engine/main_loop.h>
#include <yr/event/event_bus.h>
#include <yr/input/input_system.h>
#include <yr/scene/scene_tree.h>

namespace yr::engine {

  const char* phaseName(Phase phase) noexcept {
    static constexpr const char* kNames[kPhaseCount] = {
        "begin_frame",    "poll_input",   "flush_messages",  "flush_events", "pump_assets",
        "pre_physics",    "physics_step", "process",         "post_process", "apply_structure",
        "extract_render", "present",      "collect_garbage", "end_frame"};
    const auto index = static_cast<std::size_t>(phase);
    return index < kPhaseCount ? kNames[index] : "unknown";
  }

  MainLoop::MainLoop(scene::SceneTree& tree, evt::EventBus& events) noexcept : sceneTree_(tree), eventBus_(events) {
    stats_.setPhaseReported(Phase::kBeginFrame, true);
    stats_.setPhaseReported(Phase::kFlushEvents, true);
    stats_.setPhaseReported(Phase::kPhysicsSteps, true);
    stats_.setPhaseReported(Phase::kProcess, true);
    stats_.setPhaseReported(Phase::kPostProcess, true);
    stats_.setPhaseReported(Phase::kApplyStructure, true);
    stats_.setPhaseReported(Phase::kExtractRender, true);
    stats_.setPhaseReported(Phase::kPresent, true);
    stats_.setPhaseReported(Phase::kEndFrame, true);
  }

  void MainLoop::tick(double realDelta) {
    realDelta_ = realDelta;      // 进入阶段 0 前暂存本帧 real delta；beginFrame 算 gameDelta 并累积物理
    (void)frameTimer_.restart(); // 整帧计时的起点
    (void)phaseTimer_.restart();

    beginFrame();
    phaseDurations_[static_cast<std::size_t>(Phase::kBeginFrame)] = phaseTimer_.restart();
    pollInput();
    phaseDurations_[static_cast<std::size_t>(Phase::kPollInput)] = phaseTimer_.restart();
    flushMessages();
    phaseDurations_[static_cast<std::size_t>(Phase::kFlushMessages)] = phaseTimer_.restart();
    flushEvents();
    phaseDurations_[static_cast<std::size_t>(Phase::kFlushEvents)] = phaseTimer_.restart();
    pumpAssets();
    phaseDurations_[static_cast<std::size_t>(Phase::kPumpAssets)] = phaseTimer_.restart();
    prePhysics();
    phaseDurations_[static_cast<std::size_t>(Phase::kPrePhysics)] = phaseTimer_.restart();
    physicsSteps();
    phaseDurations_[static_cast<std::size_t>(Phase::kPhysicsSteps)] = phaseTimer_.restart();
    processPhase();
    phaseDurations_[static_cast<std::size_t>(Phase::kProcess)] = phaseTimer_.restart();
    postProcess();
    phaseDurations_[static_cast<std::size_t>(Phase::kPostProcess)] = phaseTimer_.restart();
    applyStructure();
    phaseDurations_[static_cast<std::size_t>(Phase::kApplyStructure)] = phaseTimer_.restart();
    extractRender();
    phaseDurations_[static_cast<std::size_t>(Phase::kExtractRender)] = phaseTimer_.restart();
    present();
    phaseDurations_[static_cast<std::size_t>(Phase::kPresent)] = phaseTimer_.restart();
    collectGarbage();
    phaseDurations_[static_cast<std::size_t>(Phase::kCollectGarbage)] = phaseTimer_.restart();
    endFrame();
  }

  void MainLoop::beginFrame() {
    ++frameIndex_;
    gameDelta_ = realDelta_ * effectiveTimeScale();
    accumulator_ += gameDelta_;
    physicsStepsFrame_ = 0;
  }

  void MainLoop::pollInput() {
    if (inputSystem_ != nullptr) {
      inputSystem_->beginFrame();
    }
  }

  void MainLoop::flushMessages() {
    // TODO: MessageQueue::flush（上一帧的 deferred call；MessageQueue 尚未实现）。
  }

  void MainLoop::flushEvents() {
    eventBus_.flush(); // 安全点：派发上一帧 post 的事件
  }

  void MainLoop::pumpAssets() {
    // 阶段 4：每条 tick 最多安装 1 个纹理 completion，并按顶点预算安装 mesh job。
    if (assetDatabase_ != nullptr) {
      assetDatabase_->pump(asset::kDefaultTextureCompletionsPerPump);
    }
  }

  void MainLoop::prePhysics() {
    // TODO: 派发 kPrePhysicsProcess 通知。
  }

  void MainLoop::physicsSteps() {
    // 累加器攒够一步就派发；上限防止一帧卡太久追赶不完
    int steps = 0;
    while (accumulator_ >= kPhysicsStep && steps < kMaxPhysicsSteps) {
      onPhysicsStep(kPhysicsStep);
      accumulator_ -= kPhysicsStep;
      ++steps;
    }
    physicsStepsFrame_ = steps;
    if (accumulator_ >= kPhysicsStep) {
      accumulator_ = std::fmod(accumulator_, kPhysicsStep);
      if (!spiralReported_) {
        spiralReported_ = true;
        YR_LOG_WARN("MainLoop", "physics spiral: capped at max steps this frame, backlog discarded");
      }
    }
  }

  void MainLoop::onPhysicsStep(double physicsDelta) {
    (void)physicsDelta; // 默认 no-op；physics 后端 / 测试探针覆写
  }

  void MainLoop::processPhase() {
    sceneTree_.process(static_cast<float>(gameDelta_));
  }

  void MainLoop::postProcess() {
    sceneTree_.refreshTransforms();
  }

  void MainLoop::applyStructure() {
    sceneTree_.flushDeletions();
  }

  void MainLoop::extractRender() {
    snapshot_.frame_index = frameIndex();
    renderExtract_.setShadow(shadow_);
    renderExtract_.extract(sceneTree_, camera_, snapshot_);
  }

  void MainLoop::present() {
    if (backend_ != nullptr) {
      backend_->present(snapshot_);
    }
  }

  void MainLoop::collectGarbage() {
    // 卸载仅剩 DB 持有的资源；GPU 延迟销毁后端在 present 处理
    if (assetDatabase_ != nullptr) {
      (void)assetDatabase_->collectGarbage();
    }
  }

  void MainLoop::endFrame() {
    const core::Duration endFrameCost = phaseTimer_.elapsed();
    phaseDurations_[static_cast<std::size_t>(Phase::kEndFrame)] = endFrameCost;
    stats_.recordFrame(frameTimer_.elapsed(), phaseDurations_, physicsStepsFrame_);
  }

} // namespace yr::engine
