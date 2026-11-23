#include <yr/scene/light_node.h>

#include <memory>
#include <string>

#include <yr/core/string_interner.h>
#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/scene_classes.h>

namespace yr::scene {

  namespace {

    // 短于此长度的方向视为无方向；零向量是安全输入，回退到默认朝向。
    constexpr float kMinDirectionLength = 1e-6F;

  } // namespace

  namespace {

    [[nodiscard]] core::StringId intern(const char* text) {
      return core::globalStringInterner().intern(text);
    }

    [[nodiscard]] const char* typeName(LightType type) noexcept {
      return type == LightType::kPoint ? "point" : "directional";
    }

    [[nodiscard]] bool parseType(std::string_view text, LightType& type) noexcept {
      if (text == "directional") {
        type = LightType::kDirectional;
        return true;
      }
      if (text == "point") {
        type = LightType::kPoint;
        return true;
      }
      return false;
    }

  } // namespace

  void LightNode::setIntensity(float intensity) noexcept {
    intensity_ = intensity > 0.0F ? intensity : 0.0F;
  }

  void LightNode::setRange(float range) noexcept {
    range_ = range > 0.0F ? range : 0.0F;
  }

  void LightNode::setDirection(const math::Vec3& direction) {
    const float length = direction.length();
    direction_ = length > kMinDirectionLength ? direction / length : math::Vec3{0.0F, 0.0F, -1.0F};
  }

  const obj::ClassInfo& lightNodeClassInfo() noexcept {
    using namespace obj;

    static const PropertyInfo kProperties[] = {
        {.name = intern("type"),
         .type = Variant::Type::kString,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(std::string(typeName(static_cast<const LightNode*>(object)->type())));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto text = value.tryAs<std::string>();
           if (!text.has_value()) {
             return false;
           }
           LightType type = LightType::kDirectional;
           if (!parseType(*text, type)) {
             return false;
           }
           static_cast<LightNode*>(object)->setType(type);
           return true;
         }},
        {.name = intern("color"),
         .type = Variant::Type::kVec3,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const LightNode*>(object)->color());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto color = value.tryAs<math::Vec3>();
           if (!color.has_value()) {
             return false;
           }
           static_cast<LightNode*>(object)->setColor(*color);
           return true;
         }},
        {.name = intern("intensity"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const LightNode*>(object)->intensity()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto intensity = value.tryAs<double>();
           if (!intensity.has_value()) {
             return false;
           }
           static_cast<LightNode*>(object)->setIntensity(static_cast<float>(*intensity));
           return true;
         }},
        {.name = intern("range"),
         .type = Variant::Type::kFloat,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<double>(static_cast<const LightNode*>(object)->range()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto range = value.tryAs<double>();
           if (!range.has_value()) {
             return false;
           }
           static_cast<LightNode*>(object)->setRange(static_cast<float>(*range));
           return true;
         }},
        {.name = intern("direction"),
         .type = Variant::Type::kVec3,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const LightNode*>(object)->direction());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto direction = value.tryAs<math::Vec3>();
           if (!direction.has_value()) {
             return false;
           }
           static_cast<LightNode*>(object)->setDirection(*direction);
           return true;
         }},
    };

    auto factory = []() -> std::unique_ptr<obj::Object> { return std::make_unique<LightNode>(); };
    static const obj::ClassInfo info(core::globalStringInterner().intern("LightNode"), &nodeClassInfo(), kProperties,
                                     factory);
    return info;
  }

  const obj::ClassInfo* LightNode::classInfo() const noexcept {
    return &lightNodeClassInfo();
  }

} // namespace yr::scene
