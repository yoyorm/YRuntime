#include <yr/gameplay/input_axis.h>

#include <memory>
#include <string>

#include <yr/core/string_interner.h>
#include <yr/input/input_state.h>
#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_tree.h>

namespace yr::gameplay {

  namespace {

    [[nodiscard]] core::StringId internKey(std::string_view key) {
      return core::globalStringInterner().intern(key);
    }

    [[nodiscard]] obj::Variant keyToVariant(core::StringId key) {
      return obj::Variant(std::string(key.view()));
    }

  } // namespace

  void InputAxis::setNegativeX(std::string_view key) {
    negativeX_ = internKey(key);
  }
  void InputAxis::setPositiveX(std::string_view key) {
    positiveX_ = internKey(key);
  }
  void InputAxis::setNegativeY(std::string_view key) {
    negativeY_ = internKey(key);
  }
  void InputAxis::setPositiveY(std::string_view key) {
    positiveY_ = internKey(key);
  }
  void InputAxis::setNegativeZ(std::string_view key) {
    negativeZ_ = internKey(key);
  }
  void InputAxis::setPositiveZ(std::string_view key) {
    positiveZ_ = internKey(key);
  }

  void InputAxis::onUpdate(float /*deltaTime*/) {
    const input::InputState* const state = tree() != nullptr ? tree()->inputState() : nullptr;
    if (state == nullptr) {
      return;
    }

    const bool negativeXPressed = negativeX_.valid() && state->held(negativeX_);
    const bool positiveXPressed = positiveX_.valid() && state->held(positiveX_);
    const bool negativeYPressed = negativeY_.valid() && state->held(negativeY_);
    const bool positiveYPressed = positiveY_.valid() && state->held(positiveY_);
    const bool negativeZPressed = negativeZ_.valid() && state->held(negativeZ_);
    const bool positiveZPressed = positiveZ_.valid() && state->held(positiveZ_);

    math::Vec3 next{(positiveXPressed ? 1.0F : 0.0F) - (negativeXPressed ? 1.0F : 0.0F),
                    (positiveYPressed ? 1.0F : 0.0F) - (negativeYPressed ? 1.0F : 0.0F),
                    (positiveZPressed ? 1.0F : 0.0F) - (negativeZPressed ? 1.0F : 0.0F)};
    if (normalize_ && next.lengthSquared() > 1.0F) {
      next = next.normalized();
    }

    if (next != value_) {
      value_ = next;
      emitSignal(core::globalStringInterner().intern("axis_changed"), obj::Variant(value_));
    }
  }

  const obj::ClassInfo& inputAxisClassInfo() noexcept {
    using namespace obj;

    static const PropertyInfo kProperties[] = {
        {.name = core::globalStringInterner().intern("negative_x"),
         .type = Variant::Type::kString,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return keyToVariant(static_cast<const InputAxis*>(object)->negativeX());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto text = value.tryAs<std::string>();
           if (!text.has_value()) {
             return false;
           }
           static_cast<InputAxis*>(object)->setNegativeX(*text);
           return true;
         }},
        {.name = core::globalStringInterner().intern("positive_x"),
         .type = Variant::Type::kString,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return keyToVariant(static_cast<const InputAxis*>(object)->positiveX());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto text = value.tryAs<std::string>();
           if (!text.has_value()) {
             return false;
           }
           static_cast<InputAxis*>(object)->setPositiveX(*text);
           return true;
         }},
        {.name = core::globalStringInterner().intern("negative_y"),
         .type = Variant::Type::kString,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return keyToVariant(static_cast<const InputAxis*>(object)->negativeY());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto text = value.tryAs<std::string>();
           if (!text.has_value()) {
             return false;
           }
           static_cast<InputAxis*>(object)->setNegativeY(*text);
           return true;
         }},
        {.name = core::globalStringInterner().intern("positive_y"),
         .type = Variant::Type::kString,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return keyToVariant(static_cast<const InputAxis*>(object)->positiveY());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto text = value.tryAs<std::string>();
           if (!text.has_value()) {
             return false;
           }
           static_cast<InputAxis*>(object)->setPositiveY(*text);
           return true;
         }},
        {.name = core::globalStringInterner().intern("negative_z"),
         .type = Variant::Type::kString,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return keyToVariant(static_cast<const InputAxis*>(object)->negativeZ());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto text = value.tryAs<std::string>();
           if (!text.has_value()) {
             return false;
           }
           static_cast<InputAxis*>(object)->setNegativeZ(*text);
           return true;
         }},
        {.name = core::globalStringInterner().intern("positive_z"),
         .type = Variant::Type::kString,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return keyToVariant(static_cast<const InputAxis*>(object)->positiveZ());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto text = value.tryAs<std::string>();
           if (!text.has_value()) {
             return false;
           }
           static_cast<InputAxis*>(object)->setPositiveZ(*text);
           return true;
         }},
        {.name = core::globalStringInterner().intern("normalize"),
         .type = Variant::Type::kBool,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const InputAxis*>(object)->normalize());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto enabled = value.tryAs<bool>();
           if (!enabled.has_value()) {
             return false;
           }
           static_cast<InputAxis*>(object)->setNormalize(*enabled);
           return true;
         }},
        {.name = core::globalStringInterner().intern("axis_value"),
         .type = Variant::Type::kVec3,
         .flags = static_cast<std::uint32_t>(kReadOnly),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const InputAxis*>(object)->value());
         }},
    };

    static const ClassInfo info(
        core::globalStringInterner().intern("InputAxis"), &scene::nodeClassInfo(), kProperties,
        +[]() -> std::unique_ptr<Object> { return std::make_unique<InputAxis>(); });
    return info;
  }

  const obj::ClassInfo* InputAxis::classInfo() const noexcept {
    return &inputAxisClassInfo();
  }

} // namespace yr::gameplay