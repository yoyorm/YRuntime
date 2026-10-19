#include <cmath>
#include <cstddef>
#include <yr/core/log.h>
#include <yr/engine/main_loop.h>
#include <yr/event/event_bus.h>
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
    // 只报告真正接线的阶段；空钩子（1/2/4/5/10/11/12）省略，避免日志里全是 0ns 噪音。
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
    endFrame(); // 阶段 13 自己计时并汇总入环
  }

  void MainLoop::beginFrame() {
    ++frameIndex_;
    gameDelta_ = realDelta_ * effectiveTimeScale();
    accumulator_ += gameDelta_;
    physicsStepsFrame_ = 0;
  }

  void MainLoop::pollInput() {
    // TODO: 平台输入 → InputEvent；headless 从脚本/命令注入。
  }

  void MainLoop::flushMessages() {
    // TODO: MessageQueue::flush（上一帧的 deferred call；MessageQueue 尚未实现）。
  }

  void MainLoop::flushEvents() {
    eventBus_.flush(); // 安全点：派发上一帧 post 的事件
  }

  void MainLoop::pumpAssets() {
    // TODO: AssetDatabase::pump 完成异步加载回调与失败上报。
  }

  void MainLoop::prePhysics() {
    // TODO: 派发 kPrePhysicsProcess 通知。
  }

  void MainLoop::physicsSteps() {
    // 固定步长：累加器每攒够一步就派发一次；上限防止一帧卡太久追赶不完（spiral of death）。
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
    // 阶段 7：派发 onUpdate；SceneTree 接口收 float，显式收窄。
    sceneTree_.process(static_cast<float>(gameDelta_));
  }

  void MainLoop::postProcess() {
    sceneTree_.refreshTransforms(); // 安全点：刷新 Transform 层级缓存
  }

  void MainLoop::applyStructure() {
    sceneTree_.flushDeletions(); // 唯一结构修改点：执行本帧的延迟删除
  }

  void MainLoop::extractRender() {
    // 阶段 10：取当前帧 0-based 序号，再全量抽取；frame_index 是唯一由本阶段写入的字段。
    snapshot_.frame_index = frameIndex();
    renderExtract_.extract(sceneTree_, snapshot_);
  }

  void MainLoop::present() {
    // 阶段 11：未装配后端时 no-op（headless 默认路径）。
    if (backend_ != nullptr) {
      backend_->present(snapshot_);
    }
  }

  void MainLoop::collectGarbage() {
    // TODO: AssetDatabase::collect_garbage 与延迟销毁 GPU 资源。
  }

  void MainLoop::endFrame() {
    // 汇总点：先量本阶段自身耗时，再把整帧与各阶段数据交给 FrameStats 入环 / 报告 / 写 CSV。
    // TODO: 日志 flush。
    const core::Duration endFrameCost = phaseTimer_.elapsed();
    phaseDurations_[static_cast<std::size_t>(Phase::kEndFrame)] = endFrameCost;
    stats_.recordFrame(frameTimer_.elapsed(), phaseDurations_, physicsStepsFrame_);
  }

} // namespace yr::engine