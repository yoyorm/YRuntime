#include <yr/gameplay/move_motor.h>

#include <memory>
#include <string>
#include <vector>

#include "collider_query.h"

#include <yr/core/string_interner.h>
#include <yr/gameplay/box_collider.h>
#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_tree.h>

namespace yr::gameplay {

  namespace {

    [[nodiscard]] core::StringId intern(const char* text) {
      return core::globalStringInterner().intern(text);
    }

    [[nodiscard]] const BoxCollider* findBody(const scene::Node& actor) {
      for (std::size_t index = 0; index < actor.childCount(); ++index) {
        if (const auto* collider = dynamic_cast<const BoxCollider*>(actor.childAt(index)); collider != nullptr) {
          return collider;
        }
      }
      return nullptr;
    }

  } // namespace

  void MoveMotor::setSpeed(double speed) noexcept {
    speed_ = speed > 0.0 ? speed : 0.0;
  }
  void MoveMotor::setStepDistance(double distance) noexcept {
    stepDistance_ = distance > 0.0 ? distance : 0.0;
  }

  void MoveMotor::setDirection(const math::Vec3& direction) noexcept {
    direction_ = direction;
    if (direction_.lengthSquared() > 1.0F) {
      direction_ = direction_.normalized();
    }
  }

  void MoveMotor::onUpdate(float deltaTime) {
    if (!enabled_ || parent() == nullptr || direction_.lengthSquared() == 0.0F) {
      return;
    }

    const double distance = continuous_ ? speed_ * static_cast<double>(deltaTime) : stepDistance_;
    const math::Vec3 delta = direction_ * static_cast<float>(distance);
    const math::Vec3 next = parent()->position() + delta;

    // globalTransform 是上一帧 refreshTransforms() 的结果，恰好对应当前 position()。
    // 预测盒 = 当前世界 AABB 沿世界方向平移；嵌套在旋转/缩放父节点下也成立。
    const math::Mat4 actorWorld = parent()->globalTransform();
    const BoxCollider* const body = findBody(*parent());
    const math::Vec3 half = body != nullptr ? body->halfExtents() : math::Vec3{0.1F, 0.1F, 0.1F};
    const math::Vec3 worldDelta = actorWorld.transformVector(delta);
    Aabb candidate = body != nullptr ? body->worldAabb() : makeAabb(actorWorld, half);
    candidate.minimum += worldDelta;
    candidate.maximum += worldDelta;

    if (tree() != nullptr) {
      std::vector<const BoxCollider*> colliders;
      detail::collectColliders(*tree()->root(), colliders);
      for (const BoxCollider* other : colliders) {
        if (other == body || other->parent() == parent() || !other->enabled() || !other->solid()) {
          continue;
        }
        if (overlaps(candidate, other->worldAabb())) {
          emitSignal(intern("blocked"));
          return;
        }
      }
    }

    parent()->setPosition(next);
  }

  const obj::ClassInfo& moveMotorClassInfo() noexcept {
    using namespace obj;

    static const PropertyInfo kProperties[] = {
        {.name = intern("speed"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const MoveMotor*>(object)->speed());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto speed = value.tryAs<double>();
           if (!speed.has_value()) {
             return false;
           }
           static_cast<MoveMotor*>(object)->setSpeed(*speed);
           return true;
         }},
        {.name = intern("continuous"),
         .type = Variant::Type::kBool,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const MoveMotor*>(object)->continuous());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto flag = value.tryAs<bool>();
           if (!flag.has_value()) {
             return false;
           }
           static_cast<MoveMotor*>(object)->setContinuous(*flag);
           return true;
         }},
        {.name = intern("step_distance"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const MoveMotor*>(object)->stepDistance());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto distance = value.tryAs<double>();
           if (!distance.has_value()) {
             return false;
           }
           static_cast<MoveMotor*>(object)->setStepDistance(*distance);
           return true;
         }},
        {.name = intern("direction"),
         .type = Variant::Type::kVec3,
         .flags = static_cast<std::uint32_t>(kReadOnly),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const MoveMotor*>(object)->direction());
         }},
        {.name = intern("enabled"),
         .type = Variant::Type::kBool,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const MoveMotor*>(object)->enabled());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto flag = value.tryAs<bool>();
           if (!flag.has_value()) {
             return false;
           }
           static_cast<MoveMotor*>(object)->setEnabled(*flag);
           return true;
         }},
    };

    static const MethodInfo kMethods[] = {
        {.name = intern("set_direction"),
         .fn =
             +[](Object* object, const Variant& payload) {
               const auto direction = payload.tryAs<math::Vec3>();
               if (direction.has_value()) {
                 static_cast<MoveMotor*>(object)->setDirection(*direction);
               }
             }},
        {.name = intern("stop"),
         .fn = +[](Object* object, const Variant&) { static_cast<MoveMotor*>(object)->stop(); }},
        {.name = intern("enable"),
         .fn = +[](Object* object, const Variant&) { static_cast<MoveMotor*>(object)->setEnabled(true); }},
        {.name = intern("disable"),
         .fn = +[](Object* object, const Variant&) { static_cast<MoveMotor*>(object)->setEnabled(false); }},
    };

    static const ClassInfo info(
        intern("MoveMotor"), &scene::nodeClassInfo(), kProperties,
        +[]() -> std::unique_ptr<Object> { return std::make_unique<MoveMotor>(); }, kMethods);
    return info;
  }

  const obj::ClassInfo* MoveMotor::classInfo() const noexcept {
    return &moveMotorClassInfo();
  }

} // namespace yr::gameplay