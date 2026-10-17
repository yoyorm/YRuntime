#include <catch2/catch_test_macros.hpp>

#include <yr/core/timer.h>
#include <yr/engine/frame_stats.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using yr::core::Duration;
using yr::engine::FrameStats;
using yr::engine::FrameStatsConfig;
using yr::engine::kPhaseCount;
using yr::engine::Phase;

namespace {

  std::array<Duration, kPhaseCount> emptyPhases() {
    return {};
  }

} // namespace

TEST_CASE("frame_stats: nearest-rank percentiles over a full window", "[engine][frame_stats][percentile]") {
  FrameStats stats(FrameStatsConfig{8, 0});

  for (int i = 1; i <= 8; ++i) {
    stats.recordFrame(Duration{i}, emptyPhases(), 0);
  }

  CHECK(stats.frameCount() == 8);
  CHECK(stats.windowCount() == 8);
  // 最近秩：p50 -> ceil(0.5*8)=4 -> 第 4 个（升序索引 3）= 4ns；p95 -> ceil(7.6)=8 -> 8ns。
  CHECK(stats.totalP50() == Duration{4});
  CHECK(stats.totalP95() == Duration{8});
  CHECK(stats.totalMax() == Duration{8});
}

TEST_CASE("frame_stats: ring window keeps only the most recent samples", "[engine][frame_stats][ring]") {
  FrameStats stats(FrameStatsConfig{4, 0});

  for (int i = 1; i <= 6; ++i) {
    stats.recordFrame(Duration{i}, emptyPhases(), 0);
  }

  CHECK(stats.frameCount() == 6);
  CHECK(stats.windowCount() == 4); // 1、2 已被覆盖，窗口为 {3,4,5,6}
  CHECK(stats.totalP50() == Duration{4});
  CHECK(stats.totalMax() == Duration{6});
}

TEST_CASE("frame_stats: per-phase percentiles and reported-phase flag", "[engine][frame_stats][phase]") {
  FrameStats stats(FrameStatsConfig{4, 0});

  CHECK_FALSE(stats.isPhaseReported(Phase::kProcess));
  stats.setPhaseReported(Phase::kProcess, true);
  CHECK(stats.isPhaseReported(Phase::kProcess));

  for (int i = 0; i < 4; ++i) {
    auto phases = emptyPhases();
    phases[static_cast<std::size_t>(Phase::kProcess)] = Duration{(i + 1) * 100};
    stats.recordFrame(Duration{(i + 1) * 1000}, phases, 0);
  }

  CHECK(stats.phaseP50(Phase::kProcess) == Duration{200});
  CHECK(stats.phaseMax(Phase::kProcess) == Duration{400});
  // 未报告阶段仍可查询样本，只是不进日志。
  CHECK(stats.phaseMax(Phase::kApplyStructure) == Duration{0});
}

TEST_CASE("frame_stats: physics steps accumulate across frames", "[engine][frame_stats][physics]") {
  FrameStats stats(FrameStatsConfig{8, 0});

  stats.recordFrame(Duration{10}, emptyPhases(), 1);
  stats.recordFrame(Duration{10}, emptyPhases(), 0);
  stats.recordFrame(Duration{10}, emptyPhases(), 2);

  CHECK(stats.frameCount() == 3);
  CHECK(stats.totalPhysicsSteps() == 3);
}

TEST_CASE("frame_stats: CSV export writes header and one row per frame", "[engine][frame_stats][csv]") {
  const std::string path = "/tmp/yr_frame_stats_test.csv";
  std::remove(path.c_str());

  {
    FrameStats stats(FrameStatsConfig{8, 0});
    REQUIRE(stats.enableCsv(path));
    stats.recordFrame(Duration{10}, emptyPhases(), 1);
    stats.recordFrame(Duration{20}, emptyPhases(), 0);
    stats.recordFrame(Duration{30}, emptyPhases(), 2);
    stats.finish(); // 显式关闭；析构再调也幂等
  }

  std::ifstream input(path);
  REQUIRE(input.is_open());
  std::vector<std::string> lines;
  for (std::string line; std::getline(input, line);) {
    lines.push_back(line);
  }
  input.close();
  std::remove(path.c_str());

  REQUIRE(lines.size() == 4); // header + 3 帧
  CHECK(lines[0] == "frame,total_ns,physics_steps");
  CHECK(lines[1] == "1,10,1");
  CHECK(lines[2] == "2,20,0");
  CHECK(lines[3] == "3,30,2");
}

TEST_CASE("frame_stats: disabled CSV path fails cleanly and writes nothing", "[engine][frame_stats][csv]") {
  FrameStats stats(FrameStatsConfig{4, 0});
  CHECK_FALSE(stats.enableCsv(""));
}