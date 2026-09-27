#pragma once

#include <chrono>

namespace yr::core {

  using Duration = std::chrono::nanoseconds;
  using TimePoint = std::chrono::steady_clock::time_point;

  class ScopeTimer {
  public:
    ScopeTimer() noexcept : start_(std::chrono::steady_clock::now()) {}
    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
    ScopeTimer(ScopeTimer&&) = delete;
    ScopeTimer& operator=(ScopeTimer&&) = delete;

    [[nodiscard]] Duration elapsed() const noexcept {
      return std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start_);
    }

    [[nodiscard]] Duration restart() noexcept {
      const TimePoint current = std::chrono::steady_clock::now();
      const Duration result = std::chrono::duration_cast<Duration>(current - start_);
      start_ = current;
      return result;
    }

  private:
    TimePoint start_;
  };

} // namespace yr::core
