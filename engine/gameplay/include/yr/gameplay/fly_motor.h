#pragma once

#include <yr/math/vec3.h>
#include <yr/scene/node.h>

namespace yr::gameplay {

  // 自由飞行：parent() 必须是 CameraNode。
  // direction.x = 右、direction.y = 世界向上、direction.z = 后退；
  // 前进沿相机当前视线方向，所以移动方向始终和视角对应。
  class FlyMotor final : public scene::Node {
  public:
    void onUpdate(float deltaTime) override;

    [[nodiscard]] double speed() const noexcept { return speed_; }
    void setSpeed(double speed) noexcept;
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }

    [[nodiscard]] const math::Vec3& direction() const noexcept { return direction_; }
    void setDirection(const math::Vec3& direction) noexcept;
    void stop() noexcept { direction_ = {}; }

    [[nodiscard]] const obj::ClassInfo* classInfo() const noexcept override;

  private:
    double speed_ = 4.0;
    bool enabled_ = true;
    math::Vec3 direction_{};
  };

  [[nodiscard]] const obj::ClassInfo& flyMotorClassInfo() noexcept;

} // namespace yr::gameplay