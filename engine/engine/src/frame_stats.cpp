#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <yr/core/log.h>
#include <yr/engine/frame_stats.h>

namespace yr::engine {

  namespace {

    // 人类可读的耗时：ns / us / ms 三档，保留 1 位小数。
    [[nodiscard]] std::string formatDuration(core::Duration duration) {
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

    // 最近秩百分位：先升序排序，index = ceil(fraction * n) - 1。
    [[nodiscard]] core::Duration percentileOf(std::vector<core::Duration> samples, double fraction) {
      if (samples.empty()) {
        return core::Duration::zero();
      }
      std::sort(samples.begin(), samples.end());
      const auto count = static_cast<double>(samples.size());
      auto index = static_cast<std::size_t>(std::ceil(fraction * count));
      if (index == 0) {
        index = 1;
      }
      --index;
      if (index >= samples.size()) {
        index = samples.size() - 1;
      }
      return samples[index];
    }

  } // namespace

  FrameStats::FrameStats(FrameStatsConfig config)
      : windowSize_(config.windowSize == 0 ? 1 : config.windowSize), reportInterval_(config.reportInterval) {
    window_.resize(windowSize_);
  }

  FrameStats::~FrameStats() {
    finish();
  }

  void FrameStats::recordFrame(core::Duration total, const std::array<core::Duration, kPhaseCount>& phaseDurations,
                               int physicsSteps) {
    FrameSample& sample = window_[writeIndex_];
    sample.total = total;
    sample.phases = phaseDurations;
    sample.physicsSteps = physicsSteps;

    writeIndex_ = (writeIndex_ + 1) % windowSize_;
    if (sampleCount_ < windowSize_) {
      ++sampleCount_;
    }
    ++frameCount_;
    totalPhysicsSteps_ += physicsSteps;

    writeCsvRow(total, physicsSteps);

    if (reportInterval_ != 0 && frameCount_ % reportInterval_ == 0) {
      report();
    }
  }

  void FrameStats::setPhaseReported(Phase phase, bool enabled) noexcept {
    const auto index = static_cast<std::size_t>(phase);
    if (index < kPhaseCount) {
      reportPhase_[index] = enabled;
    }
  }

  bool FrameStats::isPhaseReported(Phase phase) const noexcept {
    const auto index = static_cast<std::size_t>(phase);
    return index < kPhaseCount && reportPhase_[index];
  }

  bool FrameStats::enableCsv(const std::string& path) {
    finish(); // 重复调用先关闭旧文件
    if (path.empty()) {
      return false;
    }
    csv_.open(path, std::ios::out | std::ios::trunc);
    if (!csv_.is_open()) {
      YR_LOG_ERROR("stats", "failed to open CSV path");
      return false;
    }
    csv_ << "frame,total_ns,physics_steps\n";
    return true;
  }

  void FrameStats::finish() {
    if (csv_.is_open()) {
      csv_.flush();
      csv_.close();
    }
  }

  core::Duration FrameStats::totalPercentile(double fraction) const noexcept {
    std::vector<core::Duration> samples;
    samples.reserve(sampleCount_);
    for (std::size_t i = 0; i < sampleCount_; ++i) {
      samples.push_back(window_[i].total);
    }
    return percentileOf(std::move(samples), fraction);
  }

  core::Duration FrameStats::phasePercentile(Phase phase, double fraction) const noexcept {
    const auto phaseIndex = static_cast<std::size_t>(phase);
    if (phaseIndex >= kPhaseCount) {
      return core::Duration::zero();
    }
    std::vector<core::Duration> samples;
    samples.reserve(sampleCount_);
    for (std::size_t i = 0; i < sampleCount_; ++i) {
      samples.push_back(window_[i].phases[phaseIndex]);
    }
    return percentileOf(std::move(samples), fraction);
  }

  core::Duration FrameStats::totalP50() const noexcept {
    return totalPercentile(0.50);
  }
  core::Duration FrameStats::totalP95() const noexcept {
    return totalPercentile(0.95);
  }
  core::Duration FrameStats::totalMax() const noexcept {
    return totalPercentile(1.0);
  }
  core::Duration FrameStats::phaseP50(Phase phase) const noexcept {
    return phasePercentile(phase, 0.50);
  }
  core::Duration FrameStats::phaseP95(Phase phase) const noexcept {
    return phasePercentile(phase, 0.95);
  }
  core::Duration FrameStats::phaseMax(Phase phase) const noexcept {
    return phasePercentile(phase, 1.0);
  }

  void FrameStats::writeCsvRow(core::Duration total, int physicsSteps) {
    if (!csv_.is_open()) {
      return;
    }
    csv_ << frameCount_ << ',' << total.count() << ',' << physicsSteps << '\n';
  }

  void FrameStats::report() {
    std::string line = "frame=" + std::to_string(frameCount_);
    line += " phys=" + std::to_string(totalPhysicsSteps_);
    line += " | total p50=" + formatDuration(totalP50());
    line += " p95=" + formatDuration(totalP95());
    line += " max=" + formatDuration(totalMax());
    for (std::size_t i = 0; i < kPhaseCount; ++i) {
      if (!reportPhase_[i]) {
        continue;
      }
      line += " | ";
      line += phaseName(static_cast<Phase>(i));
      line += " p50=" + formatDuration(phaseP50(static_cast<Phase>(i)));
    }
    YR_LOG_INFO("stats", line);
  }

} // namespace yr::engine