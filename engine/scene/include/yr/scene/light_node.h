#pragma once

#include <cstdint>

#include <yr/math/vec3.h>
#include <yr/scene/node.h>

namespace yr::scene {

  enum class LightType : std::uint8_t { kDirectional, kPoint };

  class LightNode final : public Node {
  public:
    LightNode() = default;
    ~LightNode() override = default;

    [[nodiscard]] LightType type() const noexcept { return type_; }
    void setType(LightType type) noexcept { type_ = type; }

    [[nodiscard]] const math::Vec3& color() const noexcept { return color_; }
    void setColor(const math::Vec3& color) noexcept { color_ = color; }

    [[nodiscard]] float intensity() const noexcept { return intensity_; }
    void setIntensity(float intensity) noexcept;

    [[nodiscard]] float range() const noexcept { return range_; }
    void setRange(float range) noexcept;

    // 光传播方向（从光源指向场景），不是“指向光源”的方向。
    [[nodiscard]] const math::Vec3& direction() const noexcept { return direction_; }
    void setDirection(const math::Vec3& direction);

    [[nodiscard]] const obj::ClassInfo* classInfo() const noexcept override;

  private:
    LightType type_ = LightType::kDirectional;
    math::Vec3 color_{1.0F, 1.0F, 1.0F};
    float intensity_ = 1.0F;
    float range_ = 10.0F;
    math::Vec3 direction_{0.0F, 0.0F, -1.0F};
  };

  [[nodiscard]] const obj::ClassInfo& lightNodeClassInfo() noexcept;

} // namespace yr::scene
