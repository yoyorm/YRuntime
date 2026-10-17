#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <yr/core/timer.h>
#include <yr/engine/phase.h>

namespace yr::engine {

  // FrameStats 配置。windowSize 同时是报告用的滑动窗口；reportInterval = 0 表示只记录不打印。
  struct FrameStatsConfig {
    std::size_t windowSize = 4096;
    std::uint64_t reportInterval = 1000;
  };

  // 帧耗时统计：MainLoop 每帧把整帧与各阶段耗时交给 recordFrame，这里负责
  //   - 环形窗口保留最近 windowSize 帧的 total + 按阶段耗时；
  //   - 每 reportInterval 帧打印一行 total 与"已接线阶段"的 p50/p95/max；
  //   - 可选 CSV：enableCsv 后立即写 header，之后每帧追加 total + physics 步数。
  //
  // 无堆外指针，仅持有 std::vector / std::ofstream；析构时若 CSV 仍开启则关闭文件。
  class FrameStats {
  public:
    explicit FrameStats(FrameStatsConfig config = {});
    ~FrameStats();

    FrameStats(const FrameStats&) = delete;
    FrameStats& operator=(const FrameStats&) = delete;
    FrameStats(FrameStats&&) = delete;
    FrameStats& operator=(FrameStats&&) = delete;

    // 汇总点：本帧数据入环、按间隔报告、必要时写 CSV。由 MainLoop::endFrame 调用。
    void recordFrame(core::Duration total, const std::array<core::Duration, kPhaseCount>& phaseDurations,
                     int physicsSteps);

    // 是否在周期报告里输出该阶段的 per-phase 百分位。默认全部关闭，由 MainLoop 只标记真正接线的阶段。
    void setPhaseReported(Phase phase, bool enabled) noexcept;
    [[nodiscard]] bool isPhaseReported(Phase phase) const noexcept;

    void setReportInterval(std::uint64_t frames) noexcept { reportInterval_ = frames; }
    [[nodiscard]] std::uint64_t reportInterval() const noexcept { return reportInterval_; }

    // 打开 CSV 导出并写 header；路径打不开返回 false。重复调用会先关闭前一个文件。
    [[nodiscard]] bool enableCsv(const std::string& path);
    // 关闭 CSV 导出；幂等。析构会自动调用。
    void finish();

    [[nodiscard]] std::uint64_t frameCount() const noexcept { return frameCount_; }
    // 自构造以来所有帧的 physics 步数累计。
    [[nodiscard]] int totalPhysicsSteps() const noexcept { return totalPhysicsSteps_; }
    // 当前环形窗口内的样本数（<= windowSize）。
    [[nodiscard]] std::size_t windowCount() const noexcept { return sampleCount_; }
    [[nodiscard]] std::size_t windowSize() const noexcept { return windowSize_; }

    [[nodiscard]] core::Duration totalP50() const noexcept;
    [[nodiscard]] core::Duration totalP95() const noexcept;
    [[nodiscard]] core::Duration totalMax() const noexcept;
    [[nodiscard]] core::Duration phaseP50(Phase phase) const noexcept;
    [[nodiscard]] core::Duration phaseP95(Phase phase) const noexcept;
    [[nodiscard]] core::Duration phaseMax(Phase phase) const noexcept;

  private:
    struct FrameSample {
      core::Duration total{};
      std::array<core::Duration, kPhaseCount> phases{};
      int physicsSteps = 0;
    };

    void report();
    [[nodiscard]] core::Duration totalPercentile(double fraction) const noexcept;
    [[nodiscard]] core::Duration phasePercentile(Phase phase, double fraction) const noexcept;
    void writeCsvRow(core::Duration total, int physicsSteps);

    std::vector<FrameSample> window_;
    std::size_t windowSize_;
    std::size_t writeIndex_ = 0;
    std::size_t sampleCount_ = 0;
    std::uint64_t frameCount_ = 0;
    int totalPhysicsSteps_ = 0;
    std::uint64_t reportInterval_;
    std::array<bool, kPhaseCount> reportPhase_{};
    std::ofstream csv_;
  };

} // namespace yr::engine