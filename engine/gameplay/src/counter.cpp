#include <yr/gameplay/counter.h>

#include <memory>

#include <yr/core/string_interner.h>
#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/scene_classes.h>

namespace yr::gameplay {

  namespace {

    [[nodiscard]] core::StringId intern(const char* text) {
      return core::globalStringInterner().intern(text);
    }

  } // namespace

  void Counter::increment() {
    add(step_);
  }

  void Counter::add(std::int64_t amount) {
    value_ += amount;
    emitChanged();
    checkReached();
  }

  void Counter::reset() {
    value_ = 0;
    reached_ = false;
    emitChanged();
  }

  void Counter::emitChanged() {
    emitSignal(intern("changed"), obj::Variant(value_));
  }

  void Counter::checkReached() {
    if (targetValue_ > 0 && !reached_ && value_ >= targetValue_) {
      reached_ = true;
      emitSignal(intern("reached"), obj::Variant(value_));
    }
  }

  const obj::ClassInfo& counterClassInfo() noexcept {
    using namespace obj;

    static const PropertyInfo kProperties[] = {
        {.name = intern("value"),
         .type = Variant::Type::kInt,
         .flags = static_cast<std::uint32_t>(kReadOnly),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const Counter*>(object)->value());
         }},
        {.name = intern("step"),
         .type = Variant::Type::kInt,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant { return Variant(static_cast<const Counter*>(object)->step()); },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto step = value.tryAs<std::int64_t>();
           if (!step.has_value()) {
             return false;
           }
           static_cast<Counter*>(object)->setStep(*step);
           return true;
         }},
        {.name = intern("target_value"),
         .type = Variant::Type::kInt,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const Counter*>(object)->targetValue());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto target = value.tryAs<std::int64_t>();
           if (!target.has_value()) {
             return false;
           }
           static_cast<Counter*>(object)->setTargetValue(*target);
           return true;
         }},
    };

    static const MethodInfo kMethods[] = {
        {.name = intern("increment"),
         .fn = +[](Object* object, const Variant&) { static_cast<Counter*>(object)->increment(); }},
        {.name = intern("add"),
         .fn =
             +[](Object* object, const Variant& payload) {
               const auto amount = payload.tryAs<std::int64_t>();
               if (amount.has_value()) {
                 static_cast<Counter*>(object)->add(*amount);
               }
             }},
        {.name = intern("reset"),
         .fn = +[](Object* object, const Variant&) { static_cast<Counter*>(object)->reset(); }},
    };

    static const ClassInfo info(
        intern("Counter"), &scene::nodeClassInfo(), kProperties,
        +[]() -> std::unique_ptr<Object> { return std::make_unique<Counter>(); }, kMethods);
    return info;
  }

  const obj::ClassInfo* Counter::classInfo() const noexcept {
    return &counterClassInfo();
  }

} // namespace yr::gameplay