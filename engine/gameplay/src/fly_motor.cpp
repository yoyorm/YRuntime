#include <yr/gameplay/fly_motor.h>

#include <memory>

#include <yr/core/string_interner.h>
#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/camera_node.h>
#include <yr/scene/scene_classes.h>

namespace yr::gameplay {

  namespace {

    [[nodiscard]] core::StringId intern(const char* text) {
      return core::globalStringInterner().intern(text);
    }

  } // namespace

  void FlyMotor::setSpeed(double speed) noexcept {
    speed_ = speed > 0.0 ? speed : 0.0;
  }

  void FlyMotor::setDirection(const math::Vec3& direction) noexcept {
    direction_ = direction;
    if (direction_.lengthSquared() > 1.0F) {
      direction_ = direction_.normalized();
    }
  }

  void FlyMotor::onUpdate(float deltaTime) {
    auto* const camera = dynamic_cast<scene::CameraNode*>(parent());
    if (!enabled_ || camera == nullptr || direction_.lengthSquared() == 0.0F) {
      return;
    }

    const math::Vec3 forward = camera->forward();
    math::Vec3 right = math::cross(forward, math::Vec3{0.0F, 1.0F, 0.0F});
    if (right.lengthSquared() < 1e-8F) {
      right = math::Vec3{1.0F, 0.0F, 0.0F};
    }
    right = right.normalized();

    const math::Vec3 move =
        right * direction_.x + math::Vec3{0.0F, 1.0F, 0.0F} * direction_.y + forward * (-direction_.z);
    const float distance = static_cast<float>(speed_ * static_cast<double>(deltaTime));
    camera->setPosition(camera->position() + move * distance);
  }

  const obj::ClassInfo& flyMotorClassInfo() noexcept {
    using namespace obj;

    static const PropertyInfo kProperties[] = {
        {.name = intern("speed"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const FlyMotor*>(object)->speed()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto speed = value.tryAs<double>();
           if (!speed.has_value()) {
             return false;
           }
           static_cast<FlyMotor*>(object)->setSpeed(*speed);
           return true;
         }},
        {.name = intern("enabled"),
         .type = Variant::Type::kBool,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const FlyMotor*>(object)->enabled());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto flag = value.tryAs<bool>();
           if (!flag.has_value()) {
             return false;
           }
           static_cast<FlyMotor*>(object)->setEnabled(*flag);
           return true;
         }},
        {.name = intern("direction"),
         .type = Variant::Type::kVec3,
         .flags = static_cast<std::uint32_t>(kReadOnly),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const FlyMotor*>(object)->direction());
         }},
    };

    static const MethodInfo kMethods[] = {
        {.name = intern("set_direction"),
         .fn =
             +[](Object* object, const Variant& payload) {
               const auto direction = payload.tryAs<math::Vec3>();
               if (direction.has_value()) {
                 static_cast<FlyMotor*>(object)->setDirection(*direction);
               }
             }},
        {.name = intern("stop"), .fn = +[](Object* object, const Variant&) { static_cast<FlyMotor*>(object)->stop(); }},
        {.name = intern("enable"),
         .fn = +[](Object* object, const Variant&) { static_cast<FlyMotor*>(object)->setEnabled(true); }},
        {.name = intern("disable"),
         .fn = +[](Object* object, const Variant&) { static_cast<FlyMotor*>(object)->setEnabled(false); }},
    };

    static const ClassInfo info(
        intern("FlyMotor"), &scene::nodeClassInfo(), kProperties,
        +[]() -> std::unique_ptr<Object> { return std::make_unique<FlyMotor>(); }, kMethods);
    return info;
  }

  const obj::ClassInfo* FlyMotor::classInfo() const noexcept {
    return &flyMotorClassInfo();
  }

} // namespace yr::gameplay