#include <yr/gameplay/sun_sweep.h>

#include <cmath>
#include <memory>

#include <yr/core/string_interner.h>
#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/light_node.h>
#include <yr/scene/scene_classes.h>

namespace yr::gameplay {

  namespace {

    constexpr float kPi = 3.14159265358979323846F;

    [[nodiscard]] core::StringId intern(const char* text) {
      return core::globalStringInterner().intern(text);
    }

    [[nodiscard]] float wrapDegrees(float degrees) noexcept {
      while (degrees >= 360.0F) {
        degrees -= 360.0F;
      }
      while (degrees < 0.0F) {
        degrees += 360.0F;
      }
      return degrees;
    }

  } // namespace

  void SunSweep::setRadius(float radius) noexcept {
    radius_ = radius >= 0.0F ? radius : 0.0F;
  }

  void SunSweep::setHeight(float height) noexcept {
    height_ = height;
  }

  void SunSweep::setHeightAmplitude(float amplitude) noexcept {
    heightAmplitude_ = amplitude >= 0.0F ? amplitude : 0.0F;
  }

  void SunSweep::setPhaseDegrees(float degrees) noexcept {
    phaseDegrees_ = wrapDegrees(degrees);
    if (!initialized_) {
      angleDegrees_ = phaseDegrees_;
    }
  }

  void SunSweep::onUpdate(float deltaTime) {
    auto* const light = dynamic_cast<scene::LightNode*>(parent());
    if (light == nullptr) {
      return;
    }

    if (!initialized_) {
      angleDegrees_ = phaseDegrees_;
      initialized_ = true;
    }
    angleDegrees_ = wrapDegrees(angleDegrees_ + speedDegrees_ * deltaTime);

    const float radians = angleDegrees_ * kPi / 180.0F;
    const float verticalOffset = height_ + heightAmplitude_ * std::sin(radians);
    const math::Vec3 offset{std::cos(radians) * radius_, verticalOffset, std::sin(radians) * radius_};
    const math::Vec3 position = center_ + offset;
    parent()->setPosition(position);
    light->setDirection(center_ - position);
  }

  const obj::ClassInfo& sunSweepClassInfo() noexcept {
    using namespace obj;

    static const PropertyInfo kProperties[] = {
        {.name = intern("center"),
         .type = Variant::Type::kVec3,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const SunSweep*>(object)->center());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto center = value.tryAs<math::Vec3>();
           if (!center.has_value()) {
             return false;
           }
           static_cast<SunSweep*>(object)->setCenter(*center);
           return true;
         }},
        {.name = intern("radius"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const SunSweep*>(object)->radius()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto radius = value.tryAs<double>();
           if (!radius.has_value()) {
             return false;
           }
           static_cast<SunSweep*>(object)->setRadius(static_cast<float>(*radius));
           return true;
         }},
        {.name = intern("height"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const SunSweep*>(object)->height()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto height = value.tryAs<double>();
           if (!height.has_value()) {
             return false;
           }
           static_cast<SunSweep*>(object)->setHeight(static_cast<float>(*height));
           return true;
         }},
        {.name = intern("height_amplitude"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const SunSweep*>(object)->heightAmplitude()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto amplitude = value.tryAs<double>();
           if (!amplitude.has_value()) {
             return false;
           }
           static_cast<SunSweep*>(object)->setHeightAmplitude(static_cast<float>(*amplitude));
           return true;
         }},
        {.name = intern("speed_degrees"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const SunSweep*>(object)->speedDegrees()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto degrees = value.tryAs<double>();
           if (!degrees.has_value()) {
             return false;
           }
           static_cast<SunSweep*>(object)->setSpeedDegrees(static_cast<float>(*degrees));
           return true;
         }},
        {.name = intern("phase_degrees"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const SunSweep*>(object)->phaseDegrees()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto degrees = value.tryAs<double>();
           if (!degrees.has_value()) {
             return false;
           }
           static_cast<SunSweep*>(object)->setPhaseDegrees(static_cast<float>(*degrees));
           return true;
         }},
    };

    static const ClassInfo info(
        intern("SunSweep"), &scene::nodeClassInfo(), kProperties,
        +[]() -> std::unique_ptr<Object> { return std::make_unique<SunSweep>(); });
    return info;
  }

  const obj::ClassInfo* SunSweep::classInfo() const noexcept {
    return &sunSweepClassInfo();
  }

} // namespace yr::gameplay