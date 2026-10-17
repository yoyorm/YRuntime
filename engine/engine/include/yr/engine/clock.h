#pragma once

#include <chrono>

namespace yr::engine {

  // 墙钟 delta 采样器。只负责采样：timeScale / paused 归 MainLoop，physics 累加器归 MainLoop。
  //
  // 真实时钟：nextDelta() 返回距上次调用的墙钟秒数，clamp 到 [0, maxDelta]。上限防止调试断点 /
  // 挂起后单帧巨大 delta 冲垮固定步长累加器——clamp 是采样质量问题，因此放 Clock 侧。
  //
  // 测试注入：派生并覆写 now() 换成确定性时间序列；或直接对 MainLoop::tick(delta) 传值——
  // tick 是最终注入点，不经 Clock，故不受 clamp 影响。
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

    // 距上次调用的秒数；首帧从构造或上次 reset 起算，负值（时钟回拨）归零。
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

    // 以当前 now() 为起点，丢弃尚未采样的一段时间。
    void reset() noexcept { last_ = now(); }

  protected:
    // 时间源注入点；默认 steady_clock::now()。派生类覆写为脚本序列（测试用）。
    [[nodiscard]] virtual std::chrono::steady_clock::time_point now() const noexcept {
      return std::chrono::steady_clock::now();
    }

  private:
    std::chrono::steady_clock::time_point last_;
    double maxDelta_;
  };

} // namespace yr::engine