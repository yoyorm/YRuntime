#include <yr/scene/packed_scene.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <yr/asset/asset_uid.h>
#include <yr/asset/resource_types.h>
#include <yr/core/log.h>
#include <yr/core/string_interner.h>
#include <yr/math/vec3.h>
#include <yr/object/class_db.h>
#include <yr/object/class_info.h>
#include <yr/object/object.h>
#include <yr/object/object_db.h>
#include <yr/object/property_info.h>
#include <yr/object/ref.h>
#include <yr/object/variant.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_classes.h>

namespace yr::scene {

  namespace {

    [[nodiscard]] std::span<const obj::PropertyInfo> materialProperties() noexcept {
      static const obj::PropertyInfo kProperties[] = {
          {.name = core::globalStringInterner().intern("baseColorFactor"),
           .type = obj::Variant::Type::kVec3,
           .flags = static_cast<std::uint32_t>(obj::kSerialized | obj::kEditable),
           .getter = [](const obj::Object* object) -> obj::Variant {
             return obj::Variant(static_cast<const asset::Material*>(object)->baseColorFactor);
           },
           .setter = [](obj::Object* object, const obj::Variant& value) -> bool {
             const auto factor = value.tryAs<math::Vec3>();
             if (!factor.has_value()) {
               return false;
             }
             static_cast<asset::Material*>(object)->baseColorFactor = *factor;
             return true;
           }},
      };
      return kProperties;
    }

    [[nodiscard]] const obj::ClassInfo& materialClassInfo() noexcept {
      static const obj::ClassInfo info(core::globalStringInterner().intern("Material"), nullptr, materialProperties());
      return info;
    }

    void appendFace(asset::Mesh& mesh, const math::Vec3& normal, const math::Vec3& a, const math::Vec3& b,
                    const math::Vec3& c, const math::Vec3& d) {
      const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
      mesh.vertices.push_back(asset::MeshVertex{a, normal, 0.0F, 0.0F});
      mesh.vertices.push_back(asset::MeshVertex{b, normal, 1.0F, 0.0F});
      mesh.vertices.push_back(asset::MeshVertex{c, normal, 1.0F, 1.0F});
      mesh.vertices.push_back(asset::MeshVertex{d, normal, 0.0F, 1.0F});
      mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    [[nodiscard]] obj::Ref<asset::Mesh> makeBoxMesh(const math::Vec3& size) {
      if (size.x <= 0.0F || size.y <= 0.0F || size.z <= 0.0F) {
        return {};
      }
      const math::Vec3 h = size * 0.5F;
      auto mesh = obj::make_ref<asset::Mesh>();
      char path[96];
      std::snprintf(path, sizeof(path), "res://builtin/box/%.9gx%.9gx%.9g", static_cast<double>(size.x),
                    static_cast<double>(size.y), static_cast<double>(size.z));
      mesh->setUid(asset::AssetUID::fromPath(path));
      mesh->setSourcePath(path);
      (void)mesh->markLoading();
      mesh->vertices.reserve(24);
      mesh->indices.reserve(36);

      appendFace(*mesh, math::Vec3{1.0F, 0.0F, 0.0F}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {h.x, h.y, h.z},
                 {h.x, -h.y, h.z});
      appendFace(*mesh, math::Vec3{-1.0F, 0.0F, 0.0F}, {-h.x, -h.y, h.z}, {-h.x, h.y, h.z}, {-h.x, h.y, -h.z},
                 {-h.x, -h.y, -h.z});
      appendFace(*mesh, math::Vec3{0.0F, 1.0F, 0.0F}, {-h.x, h.y, -h.z}, {-h.x, h.y, h.z}, {h.x, h.y, h.z},
                 {h.x, h.y, -h.z});
      appendFace(*mesh, math::Vec3{0.0F, -1.0F, 0.0F}, {-h.x, -h.y, h.z}, {-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z},
                 {h.x, -h.y, h.z});
      appendFace(*mesh, math::Vec3{0.0F, 0.0F, 1.0F}, {-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z},
                 {-h.x, h.y, h.z});
      appendFace(*mesh, math::Vec3{0.0F, 0.0F, -1.0F}, {h.x, -h.y, -h.z}, {-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z},
                 {h.x, h.y, -h.z});

      mesh->boundsMin = math::Vec3{-h.x, -h.y, -h.z};
      mesh->boundsMax = math::Vec3{h.x, h.y, h.z};
      mesh->hasBounds = true;
      (void)mesh->markReady();
      return mesh;
    }

    [[nodiscard]] obj::Ref<asset::Material> makeMaterial(const PackedSubResource& packed) {
      auto material = obj::make_ref<asset::Material>();
      const std::string path = "res://builtin/material/" + packed.id;
      material->setUid(asset::AssetUID::fromPath(path));
      material->setSourcePath(path);
      (void)material->markLoading();

      for (const PackedSubResourceProperty& property : packed.properties) {
        if (property.value.isReference()) {
          return {};
        }
        if (!materialClassInfo().set_property(material.get(), property.name, property.value.value)) {
          return {};
        }
      }
      (void)material->markReady();
      return material;
    }

    [[nodiscard]] obj::Ref<asset::Resource> createSubResource(const PackedSubResource& packed) {
      if (packed.type.view() == "Material") {
        return makeMaterial(packed);
      }
      if (packed.type.view() == "BoxMesh") {
        math::Vec3 size{1.0F, 1.0F, 1.0F};
        for (const PackedSubResourceProperty& property : packed.properties) {
          if (property.value.isReference() || property.name.view() != "size") {
            return {};
          }
          const auto parsed = property.value.value.tryAs<math::Vec3>();
          if (!parsed.has_value()) {
            return {};
          }
          size = *parsed;
        }
        return makeBoxMesh(size);
      }
      return {};
    }

    [[nodiscard]] bool resolveSubResource(const SceneReference& reference,
                                          const std::vector<obj::Ref<asset::Resource>>& subResources,
                                          core::ObjectID& out) noexcept {
      if (reference.kind != ReferenceKind::kSubResource || reference.index >= subResources.size()) {
        return false;
      }
      const obj::Ref<asset::Resource>& resource = subResources[reference.index];
      if (!resource.valid()) {
        return false;
      }
      out = resource->id();
      return true;
    }

  } // namespace

  // 实例化：ClassDB 创建全部 Node；setName/set_property；
  // 按 parent_index addChild；失败返回 nullptr。
  std::unique_ptr<Node> PackedScene::instantiate(const obj::ClassDB& classes) const {
    // 创建每个 Node；根必须没有 parent。
    if (nodes.empty() || nodes.front().parent_index != kInvalidParent) {
      return nullptr;
    }

    std::vector<obj::Ref<asset::Resource>> subResources;
    subResources.reserve(sub_resources.size());
    for (const PackedSubResource& packed : sub_resources) {
      obj::Ref<asset::Resource> resource = createSubResource(packed);
      if (!resource.valid()) {
        return nullptr;
      }
      subResources.push_back(std::move(resource));
    }

    std::vector<std::unique_ptr<Node>> owners;
    std::vector<Node*> raw;
    owners.reserve(nodes.size());
    raw.reserve(nodes.size());

    for (const PackedNode& packed : nodes) {
      std::unique_ptr<obj::Object> owned = classes.instantiate(packed.type);
      if (!owned) {
        return nullptr; // 未注册
      }
      const obj::ClassInfo* const info = owned->classInfo();
      if (info == nullptr || !info->isA(&nodeClassInfo())) {
        return nullptr; // 不是 Node 或者派生类
      }

      std::unique_ptr<Node> node(static_cast<Node*>(owned.release()));
      node->setName(packed.name);

      for (const SceneProperty& property : packed.properties) {
        if (property.value.isReference()) {
          core::ObjectID id;
          if (!resolveSubResource(property.value.reference, subResources, id)) {
            return nullptr;
          }
          if (!info->set_property(node.get(), property.name, obj::Variant(id))) {
            return nullptr;
          }
          continue;
        }
        if (!info->set_property(node.get(), property.name, property.value.value)) {
          return nullptr;
        }
      }
      raw.push_back(node.get());
      owners.push_back(std::move(node));
    }

    // 按 parent_index 创建树。
    for (std::size_t i = 1; i < nodes.size(); ++i) {
      const std::size_t parentIndex = nodes[i].parent_index;
      if (parentIndex == kInvalidParent || parentIndex >= i) {
        return nullptr;
      }

      Node* const parent = raw[parentIndex];
      if (parent == nullptr || !parent->addChild(std::move(owners[i]))) {
        return nullptr;
      }
    }

    // 接线：connection.from 的命名信号 -> connection.to 的 onSignal(method)。
    for (const PackedConnection& connection : connections) {
      if (connection.from >= raw.size() || connection.to >= raw.size()) {
        return nullptr;
      }

      Node* const target = raw[connection.to];
      const obj::ClassInfo* const targetInfo = target->classInfo();
      if (targetInfo == nullptr || targetInfo->findMethod(connection.method) == nullptr) {
        YR_LOG_ERROR("scene", "connection method 未注册：node='" + std::string(target->name().view()) + "' method='" +
                                  std::string(connection.method.view()) + "'");
        return nullptr;
      }

      raw[connection.from]->connect(connection.signal, target->id(), connection.method);
    }

    return std::move(owners[0]); // root
  }

} // namespace yr::scene