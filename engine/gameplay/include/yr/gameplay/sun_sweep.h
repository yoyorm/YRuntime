#pragma once

#include <yr/math/vec3.h>
#include <yr/scene/node.h>

namespace yr::gameplay {

  // 让 parent()（应为 LightNode）沿圆周持续移动，并始终指向 center。
  // height_amplitude 让高度随角度起伏：位置、方位角、俯角同时变化，
  // 适合做“太阳从不同角度照进建筑”的光照基准。
  // 假设 parent 的祖先变换为 identity：写回的是局部 position，即世界坐标。
  class SunSweep final : public scene::Node {
  public:
    void onUpdate(float deltaTime) override;

    [[nodiscard]] const math::Vec3& center() const noexcept { return center_; }
    void setCenter(const math::Vec3& center) noexcept { center_ = center; }
    [[nodiscard]] float radius() const noexcept { return radius_; }
    void setRadius(float radius) noexcept;
    [[nodiscard]] float height() const noexcept { return height_; }
    void setHeight(float height) noexcept;
    [[nodiscard]] float heightAmplitude() const noexcept { return heightAmplitude_; }
    void setHeightAmplitude(float amplitude) noexcept;
    [[nodiscard]] float speedDegrees() const noexcept { return speedDegrees_; }
    void setSpeedDegrees(float degreesPerSecond) noexcept { speedDegrees_ = degreesPerSecond; }
    [[nodiscard]] float phaseDegrees() const noexcept { return phaseDegrees_; }
    void setPhaseDegrees(float degrees) noexcept;

    [[nodiscard]] const obj::ClassInfo* classInfo() const noexcept override;

  private:
    math::Vec3 center_{};
    float radius_ = 10.0F;
    float height_ = 10.0F;
    float heightAmplitude_ = 0.0F;
    float speedDegrees_ = 10.0F;
    float phaseDegrees_ = 0.0F;
    bool initialized_ = false;
    float angleDegrees_ = 0.0F;
  };

  [[nodiscard]] const obj::ClassInfo& sunSweepClassInfo() noexcept;

} // namespace yr::gameplay