#pragma once

#include <yr/math/vec3.h>
#include <yr/scene/node.h>

namespace yr::gameplay {

  // 接收方向意图并移动 parent()。
  // continuous=true：每帧 speed * dt；false：每帧 direction * step_distance（不做网格吸附）。
  // 遇 solid BoxCollider 阻挡时原地不动并发出 blocked（阻挡期间每帧都会发）。
  class MoveMotor final : public scene::Node {
  public:
    void onUpdate(float deltaTime) override;

    [[nodiscard]] double speed() const noexcept { return speed_; }
    void setSpeed(double speed) noexcept;
    [[nodiscard]] bool continuous() const noexcept { return continuous_; }
    void setContinuous(bool continuous) noexcept { continuous_ = continuous; }
    [[nodiscard]] double stepDistance() const noexcept { return stepDistance_; }
    void setStepDistance(double distance) noexcept;

    [[nodiscard]] const math::Vec3& direction() const noexcept { return direction_; }
    void setDirection(const math::Vec3& direction) noexcept;
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }

    void stop() noexcept { direction_ = {}; }

    [[nodiscard]] const obj::ClassInfo* classInfo() const noexcept override;

  private:
    double speed_ = 3.0;
    double stepDistance_ = 0.2;
    bool continuous_ = true;
    bool enabled_ = true;
    math::Vec3 direction_{};
  };

  [[nodiscard]] const obj::ClassInfo& moveMotorClassInfo() noexcept;

} // namespace yr::gameplay