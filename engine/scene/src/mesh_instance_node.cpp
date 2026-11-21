#include <yr/scene/mesh_instance_node.h>

#include <memory>
#include <span>

#include <yr/core/string_interner.h>
#include <yr/object/class_info.h>
#include <yr/object/object_db.h>
#include <yr/object/property_info.h>
#include <yr/object/ref.h>
#include <yr/object/variant.h>
#include <yr/scene/scene_classes.h>

namespace yr::scene {

  namespace {

    [[nodiscard]] std::span<const obj::PropertyInfo> meshProperties() noexcept {
      static const obj::PropertyInfo kProperties[] = {
          {.name = core::globalStringInterner().intern("mesh"),
           .type = obj::Variant::Type::kObjectID,
           .flags = static_cast<std::uint32_t>(obj::kSerialized | obj::kEditable),
           .class_hint = core::globalStringInterner().intern("Mesh"),
           .getter = [](const obj::Object* object) -> obj::Variant {
             const auto& mesh = static_cast<const MeshInstanceNode*>(object)->mesh();
             return mesh.valid() ? obj::Variant(mesh->id()) : obj::Variant{};
           },
           .setter = [](obj::Object* object, const obj::Variant& value) -> bool {
             const auto id = value.tryAs<core::ObjectID>();
             if (!id.has_value()) {
               return false;
             }
             auto* const mesh = dynamic_cast<asset::Mesh*>(obj::globalObjectDB().get(*id));
             if (mesh == nullptr) {
               return false;
             }
             static_cast<MeshInstanceNode*>(object)->setMesh(obj::Ref<asset::Mesh>(mesh));
             return true;
           }},
          {.name = core::globalStringInterner().intern("material"),
           .type = obj::Variant::Type::kObjectID,
           .flags = static_cast<std::uint32_t>(obj::kSerialized | obj::kEditable),
           .class_hint = core::globalStringInterner().intern("Material"),
           .getter = [](const obj::Object* object) -> obj::Variant {
             const auto& material = static_cast<const MeshInstanceNode*>(object)->material();
             return material.valid() ? obj::Variant(material->id()) : obj::Variant{};
           },
           .setter = [](obj::Object* object, const obj::Variant& value) -> bool {
             const auto id = value.tryAs<core::ObjectID>();
             if (!id.has_value()) {
               return false;
             }
             auto* const material = dynamic_cast<asset::Material*>(obj::globalObjectDB().get(*id));
             if (material == nullptr) {
               return false;
             }
             static_cast<MeshInstanceNode*>(object)->setMaterial(obj::Ref<asset::Material>(material));
             return true;
           }},
      };
      return kProperties;
    }

  } // namespace

  const obj::ClassInfo& meshInstanceNodeClassInfo() noexcept {
    auto factory = []() -> std::unique_ptr<obj::Object> { return std::make_unique<MeshInstanceNode>(); };
    static const obj::ClassInfo info(core::globalStringInterner().intern("MeshInstanceNode"), &nodeClassInfo(),
                                     meshProperties(), factory);
    return info;
  }

  const obj::ClassInfo* MeshInstanceNode::classInfo() const noexcept {
    return &meshInstanceNodeClassInfo();
  }

} // namespace yr::scene