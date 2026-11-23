#pragma once

#include <string_view>

#include <yr/core/string_id.h>
#include <yr/math/vec3.h>
#include <yr/scene/node.h>

namespace yr::gameplay {

  // 把 InputState 的按键状态翻译成二维方向。只依赖 SceneTree::inputState()。
  // 只在值变化时发 axis_changed(Vec3)；inputState 为空时保持上一次的值。
  class InputAxis final : public scene::Node {
  public:
    void onUpdate(float deltaTime) override;

    [[nodiscard]] const math::Vec3& value() const noexcept { return value_; }

    // 反射入口：键名是 .scn 里的字符串，内部驻留成 StringId。
    [[nodiscard]] core::StringId negativeX() const noexcept { return negativeX_; }
    [[nodiscard]] core::StringId positiveX() const noexcept { return positiveX_; }
    [[nodiscard]] core::StringId negativeY() const noexcept { return negativeY_; }
    [[nodiscard]] core::StringId positiveY() const noexcept { return positiveY_; }
    [[nodiscard]] core::StringId negativeZ() const noexcept { return negativeZ_; }
    [[nodiscard]] core::StringId positiveZ() const noexcept { return positiveZ_; }
    void setNegativeX(std::string_view key);
    void setPositiveX(std::string_view key);
    void setNegativeY(std::string_view key);
    void setPositiveY(std::string_view key);
    void setNegativeZ(std::string_view key);
    void setPositiveZ(std::string_view key);

    [[nodiscard]] bool normalize() const noexcept { return normalize_; }
    void setNormalize(bool normalize) noexcept { normalize_ = normalize; }

    [[nodiscard]] const obj::ClassInfo* classInfo() const noexcept override;

  private:
    core::StringId negativeX_{};
    core::StringId positiveX_{};
    core::StringId negativeY_{};
    core::StringId positiveY_{};
    core::StringId negativeZ_{};
    core::StringId positiveZ_{};
    bool normalize_ = true;
    math::Vec3 value_{};
  };

  [[nodiscard]] const obj::ClassInfo& inputAxisClassInfo() noexcept;

} // namespace yr::gameplay