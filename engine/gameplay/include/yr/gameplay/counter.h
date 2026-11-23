#pragma once

#include <cstdint>

#include <yr/scene/node.h>

namespace yr::gameplay {

  // 可组合计数/状态节点。
  // value 是运行状态，不进 .scn；step / target_value 进 .scn。
  // target_value <= 0 表示不检查；reached 只发一次，直到 reset() 重新武装。
  class Counter final : public scene::Node {
  public:
    [[nodiscard]] std::int64_t value() const noexcept { return value_; }
    [[nodiscard]] std::int64_t step() const noexcept { return step_; }
    [[nodiscard]] std::int64_t targetValue() const noexcept { return targetValue_; }
    void setStep(std::int64_t step) noexcept { step_ = step; }
    void setTargetValue(std::int64_t target) noexcept { targetValue_ = target; }

    void increment();
    void add(std::int64_t amount);
    void reset();

    [[nodiscard]] const obj::ClassInfo* classInfo() const noexcept override;

  private:
    void emitChanged();
    void checkReached();

    std::int64_t value_ = 0;
    std::int64_t step_ = 1;
    std::int64_t targetValue_ = 0;
    bool reached_ = false;
  };

  [[nodiscard]] const obj::ClassInfo& counterClassInfo() noexcept;

} // namespace yr::gameplay