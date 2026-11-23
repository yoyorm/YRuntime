#pragma once

#include <yr/math/vec3.h>
#include <yr/scene/node.h>

namespace yr::scene {

  // 位置来自 Node transform；朝向用 yaw/pitch（度），不依赖 rotation/scale 序列化。
  // yaw = 0、pitch = 0 时看向 -Z；yaw 绕 +Y 正值向右转，pitch 正值抬头。
  class CameraNode final : public Node {
  public:
    [[nodiscard]] float fovDegrees() const noexcept { return fovDegrees_; }
    void setFovDegrees(float degrees) noexcept;

    [[nodiscard]] float nearPlane() const noexcept { return nearPlane_; }
    void setNearPlane(float distance) noexcept;

    [[nodiscard]] float farPlane() const noexcept { return farPlane_; }
    void setFarPlane(float distance) noexcept;

    [[nodiscard]] float yawDegrees() const noexcept { return yawDegrees_; }
    [[nodiscard]] float pitchDegrees() const noexcept { return pitchDegrees_; }
    void setYawDegrees(float degrees) noexcept;
    void setPitchDegrees(float degrees) noexcept;
    void addYawDegrees(float degrees) noexcept;
    void addPitchDegrees(float degrees) noexcept;

    // 鼠标增量：Vec3(dx_deg, dy_deg, 0)，y 正 = 抬头。
    void look(const math::Vec3& deltaDegrees) noexcept;

    // 世界空间单位前向；由 yaw/pitch 计算。
    [[nodiscard]] math::Vec3 forward() const;

    [[nodiscard]] const obj::ClassInfo* classInfo() const noexcept override;

  private:
    float fovDegrees_ = 60.0F;
    float nearPlane_ = 0.1F;
    float farPlane_ = 1000.0F;
    float yawDegrees_ = 0.0F;
    float pitchDegrees_ = 0.0F;
  };

  [[nodiscard]] const obj::ClassInfo& cameraNodeClassInfo() noexcept;

} // namespace yr::scene