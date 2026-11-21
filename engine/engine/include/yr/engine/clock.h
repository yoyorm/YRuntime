#pragma once

#include <chrono>

namespace yr::engine {

  // 时钟 delta 采样器
  class Clock {
  public:
    static constexpr double kDefaultMaxDelta = 0.25;

    explicit Clock(double maxDelta = kDefaultMaxDelta) noexcept
        : last_(std::chrono::steady_clock::now()), maxDelta_(maxDelta > 0.0 ? maxDelta : 0.0) {}

    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
    Clock(Clock&&) = delete;
    Clock& operator=(Clock&&) = delete;

    virtual ~Clock() = default;

    [[nodiscard]] double nextDelta() noexcept {
      const std::chrono::steady_clock::time_point current = now();
      double delta = std::chrono::duration<double>(current - last_).count();
      last_ = current;
      if (delta < 0.0) {
        delta = 0.0;
      } else if (delta > maxDelta_) {
        delta = maxDelta_;
      }
      return delta;
    }

    void reset() noexcept { last_ = now(); }

  protected:
    [[nodiscard]] virtual std::chrono::steady_clock::time_point now() const noexcept {
      return std::chrono::steady_clock::now();
    }

  private:
    std::chrono::steady_clock::time_point last_;
    double maxDelta_;
  };

} // namespace yr::engine