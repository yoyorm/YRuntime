#include <yr/gameplay/mouse_look.h>

#include <memory>

#include <yr/core/string_interner.h>
#include <yr/input/input_state.h>
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

  } // namespace

  void MouseLook::setSensitivity(float degreesPerPixel) noexcept {
    sensitivity_ = degreesPerPixel >= 0.0F ? degreesPerPixel : 0.0F;
  }

  void MouseLook::onUpdate(float /*deltaTime*/) {
    const input::InputState* const state = tree() != nullptr ? tree()->inputState() : nullptr;
    if (state == nullptr) {
      return;
    }

    const float deltaX = state->mouseDeltaX();
    const float deltaY = state->mouseDeltaY();
    if (deltaX == 0.0F && deltaY == 0.0F) {
      return;
    }

    // 鼠标向下是屏幕坐标正方向；默认抬头的角度取反。
    const float yaw = deltaX * sensitivity_;
    const float pitch = (invertY_ ? deltaY : -deltaY) * sensitivity_;
    emitSignal(intern("look"), obj::Variant(math::Vec3{yaw, pitch, 0.0F}));
  }

  const obj::ClassInfo& mouseLookClassInfo() noexcept {
    using namespace obj;

    static const PropertyInfo kProperties[] = {
        {.name = intern("sensitivity"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const MouseLook*>(object)->sensitivity()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto degrees = value.tryAs<double>();
           if (!degrees.has_value()) {
             return false;
           }
           static_cast<MouseLook*>(object)->setSensitivity(static_cast<float>(*degrees));
           return true;
         }},
        {.name = intern("invert_y"),
         .type = Variant::Type::kBool,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const MouseLook*>(object)->invertY());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto invert = value.tryAs<bool>();
           if (!invert.has_value()) {
             return false;
           }
           static_cast<MouseLook*>(object)->setInvertY(*invert);
           return true;
         }},
    };

    static const ClassInfo info(
        intern("MouseLook"), &scene::nodeClassInfo(), kProperties,
        +[]() -> std::unique_ptr<Object> { return std::make_unique<MouseLook>(); });
    return info;
  }

  const obj::ClassInfo* MouseLook::classInfo() const noexcept {
    return &mouseLookClassInfo();
  }

} // namespace yr::gameplay