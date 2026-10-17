#pragma once

#include <cstddef>
#include <cstdint>

namespace yr::engine {

  // 帧阶段编号固定为 0..13，权威定义见 doc/01-architecture.md §5；任何改动都要写 ADR。
  enum class Phase : std::uint8_t {
    kBeginFrame = 0,
    kPollInput = 1,
    kFlushMessages = 2,
    kFlushEvents = 3,
    kPumpAssets = 4,
    kPrePhysics = 5,
    kPhysicsSteps = 6,
    kProcess = 7,
    kPostProcess = 8,
    kApplyStructure = 9,
    kExtractRender = 10,
    kPresent = 11,
    kCollectGarbage = 12,
    kEndFrame = 13,
    kCount = 14,
  };

  inline constexpr std::size_t kPhaseCount = static_cast<std::size_t>(Phase::kCount);

  // 阶段名用于日志与测试探针；越界返回 "unknown"。
  [[nodiscard]] const char* phaseName(Phase phase) noexcept;

} // namespace yr::engine