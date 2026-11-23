#include <yr/scene/camera_node.h>

#include <cmath>
#include <memory>
#include <string>

#include <yr/core/string_interner.h>
#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/scene_classes.h>

namespace yr::scene {

  namespace {

    constexpr float kPitchLimit = 89.0F;
    constexpr float kPi = 3.14159265358979323846F;

    [[nodiscard]] core::StringId intern(const char* text) {
      return core::globalStringInterner().intern(text);
    }

    [[nodiscard]] float wrapDegrees(float degrees) noexcept {
      while (degrees >= 180.0F) {
        degrees -= 360.0F;
      }
      while (degrees < -180.0F) {
        degrees += 360.0F;
      }
      return degrees;
    }

    [[nodiscard]] float clampPitch(float degrees) noexcept {
      if (degrees > kPitchLimit) {
        return kPitchLimit;
      }
      if (degrees < -kPitchLimit) {
        return -kPitchLimit;
      }
      return degrees;
    }

  } // namespace

  void CameraNode::setFovDegrees(float degrees) noexcept {
    if (degrees < 1.0F) {
      fovDegrees_ = 1.0F;
    } else if (degrees > 179.0F) {
      fovDegrees_ = 179.0F;
    } else {
      fovDegrees_ = degrees;
    }
  }

  void CameraNode::setNearPlane(float distance) noexcept {
    nearPlane_ = distance > 0.001F ? distance : 0.001F;
    if (farPlane_ <= nearPlane_) {
      farPlane_ = nearPlane_ + 0.001F;
    }
  }

  void CameraNode::setFarPlane(float distance) noexcept {
    farPlane_ = distance > nearPlane_ ? distance : nearPlane_ + 0.001F;
  }

  void CameraNode::setYawDegrees(float degrees) noexcept {
    yawDegrees_ = wrapDegrees(degrees);
  }

  void CameraNode::setPitchDegrees(float degrees) noexcept {
    pitchDegrees_ = clampPitch(degrees);
  }

  void CameraNode::addYawDegrees(float degrees) noexcept {
    setYawDegrees(yawDegrees_ + degrees);
  }

  void CameraNode::addPitchDegrees(float degrees) noexcept {
    setPitchDegrees(pitchDegrees_ + degrees);
  }

  void CameraNode::look(const math::Vec3& deltaDegrees) noexcept {
    addYawDegrees(deltaDegrees.x);
    addPitchDegrees(deltaDegrees.y);
  }

  math::Vec3 CameraNode::forward() const {
    const float yaw = yawDegrees_ * kPi / 180.0F;
    const float pitch = pitchDegrees_ * kPi / 180.0F;
    const float horizontal = std::cos(pitch);
    return math::Vec3{horizontal * std::sin(yaw), std::sin(pitch), -horizontal * std::cos(yaw)};
  }

  const obj::ClassInfo& cameraNodeClassInfo() noexcept {
    using namespace obj;

    static const PropertyInfo kProperties[] = {
        {.name = intern("yaw_degrees"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const CameraNode*>(object)->yawDegrees()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto degrees = value.tryAs<double>();
           if (!degrees.has_value()) {
             return false;
           }
           static_cast<CameraNode*>(object)->setYawDegrees(static_cast<float>(*degrees));
           return true;
         }},
        {.name = intern("pitch_degrees"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const CameraNode*>(object)->pitchDegrees()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto degrees = value.tryAs<double>();
           if (!degrees.has_value()) {
             return false;
           }
           static_cast<CameraNode*>(object)->setPitchDegrees(static_cast<float>(*degrees));
           return true;
         }},
        {.name = intern("fov_degrees"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const CameraNode*>(object)->fovDegrees()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto degrees = value.tryAs<double>();
           if (!degrees.has_value()) {
             return false;
           }
           static_cast<CameraNode*>(object)->setFovDegrees(static_cast<float>(*degrees));
           return true;
         }},
        {.name = intern("near_plane"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const CameraNode*>(object)->nearPlane()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto distance = value.tryAs<double>();
           if (!distance.has_value()) {
             return false;
           }
           static_cast<CameraNode*>(object)->setNearPlane(static_cast<float>(*distance));
           return true;
         }},
        {.name = intern("far_plane"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const CameraNode*>(object)->farPlane()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto distance = value.tryAs<double>();
           if (!distance.has_value()) {
             return false;
           }
           static_cast<CameraNode*>(object)->setFarPlane(static_cast<float>(*distance));
           return true;
         }},
    };

    static const MethodInfo kMethods[] = {
        {.name = intern("look"),
         .fn =
             +[](Object* object, const Variant& payload) {
               const auto delta = payload.tryAs<math::Vec3>();
               if (delta.has_value()) {
                 static_cast<CameraNode*>(object)->look(*delta);
               }
             }},
        {.name = intern("add_yaw"),
         .fn =
             +[](Object* object, const Variant& payload) {
               const auto degrees = payload.tryAs<double>();
               if (degrees.has_value()) {
                 static_cast<CameraNode*>(object)->addYawDegrees(static_cast<float>(*degrees));
               }
             }},
        {.name = intern("add_pitch"),
         .fn =
             +[](Object* object, const Variant& payload) {
               const auto degrees = payload.tryAs<double>();
               if (degrees.has_value()) {
                 static_cast<CameraNode*>(object)->addPitchDegrees(static_cast<float>(*degrees));
               }
             }},
    };

    static const ClassInfo info(
        intern("CameraNode"), &nodeClassInfo(), kProperties,
        +[]() -> std::unique_ptr<Object> { return std::make_unique<CameraNode>(); }, kMethods);
    return info;
  }

  const obj::ClassInfo* CameraNode::classInfo() const noexcept {
    return &cameraNodeClassInfo();
  }

} // namespace yr::scene