#include <yr/gameplay/key_trigger.h>

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

    [[nodiscard]] core::StringId intern(const char* text) {
      return core::globalStringInterner().intern(text);
    }

    [[nodiscard]] const char* phaseName(input::KeyPhase phase) noexcept {
      switch (phase) {
      case input::KeyPhase::kPressed:
        return "pressed";
      case input::KeyPhase::kReleased:
        return "released";
      case input::KeyPhase::kRepeat:
        return "repeat";
      case input::KeyPhase::kHeld:
        return "held";
      case input::KeyPhase::kTextInput:
        return "text";
      }
      return "pressed";
    }

    [[nodiscard]] bool parsePhase(std::string_view text, input::KeyPhase& phase) noexcept {
      if (text == "pressed") {
        phase = input::KeyPhase::kPressed;
        return true;
      }
      if (text == "released") {
        phase = input::KeyPhase::kReleased;
        return true;
      }
      if (text == "repeat") {
        phase = input::KeyPhase::kRepeat;
        return true;
      }
      if (text == "held") {
        phase = input::KeyPhase::kHeld;
        return true;
      }
      return false;
    }

  } // namespace

  void KeyTrigger::setKey(std::string_view key) {
    key_ = core::globalStringInterner().intern(key);
  }

  bool KeyTrigger::setPhase(std::string_view phase) {
    input::KeyPhase parsed = input::KeyPhase::kPressed;
    if (!parsePhase(phase, parsed)) {
      return false;
    }
    phase_ = parsed;
    return true;
  }

  void KeyTrigger::onUpdate(float /*deltaTime*/) {
    const input::InputState* const state = tree() != nullptr ? tree()->inputState() : nullptr;
    if (state == nullptr || !key_.valid()) {
      return;
    }

    bool triggered = false;
    switch (phase_) {
    case input::KeyPhase::kPressed:
      triggered = state->pressed(key_);
      break;
    case input::KeyPhase::kReleased:
      triggered = state->released(key_);
      break;
    case input::KeyPhase::kRepeat:
      triggered = state->repeated(key_);
      break;
    case input::KeyPhase::kHeld:
      triggered = state->held(key_);
      break;
    case input::KeyPhase::kTextInput:
      break;
    }

    if (triggered) {
      emitSignal(intern("triggered"));
    }
  }

  const obj::ClassInfo& keyTriggerClassInfo() noexcept {
    using namespace obj;

    static const PropertyInfo kProperties[] = {
        {.name = intern("key"),
         .type = Variant::Type::kString,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(std::string(static_cast<const KeyTrigger*>(object)->key().view()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto text = value.tryAs<std::string>();
           if (!text.has_value()) {
             return false;
           }
           static_cast<KeyTrigger*>(object)->setKey(*text);
           return true;
         }},
        {.name = intern("phase"),
         .type = Variant::Type::kString,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(std::string(phaseName(static_cast<const KeyTrigger*>(object)->phase())));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto text = value.tryAs<std::string>();
           if (!text.has_value()) {
             return false;
           }
           return static_cast<KeyTrigger*>(object)->setPhase(*text);
         }},
    };

    static const ClassInfo info(
        intern("KeyTrigger"), &scene::nodeClassInfo(), kProperties,
        +[]() -> std::unique_ptr<Object> { return std::make_unique<KeyTrigger>(); });
    return info;
  }

  const obj::ClassInfo* KeyTrigger::classInfo() const noexcept {
    return &keyTriggerClassInfo();
  }

} // namespace yr::gameplay