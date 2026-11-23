#pragma once

#include <yr/scene/node.h>

namespace yr::gameplay {

  // 把鼠标增量换算成角度，发出 look(Vec3(dx_deg, dy_deg, 0))。
  // 通常连到 CameraNode 的 look 方法；sensitivity 单位是度/像素。
  class MouseLook final : public scene::Node {
  public:
    void onUpdate(float deltaTime) override;

    [[nodiscard]] float sensitivity() const noexcept { return sensitivity_; }
    void setSensitivity(float degreesPerPixel) noexcept;
    [[nodiscard]] bool invertY() const noexcept { return invertY_; }
    void setInvertY(bool invert) noexcept { invertY_ = invert; }

    [[nodiscard]] const obj::ClassInfo* classInfo() const noexcept override;

  private:
    float sensitivity_ = 0.15F;
    bool invertY_ = false;
  };

  [[nodiscard]] const obj::ClassInfo& mouseLookClassInfo() noexcept;

} // namespace yr::gameplay