#include <yr/gameplay/box_collider.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "collider_query.h"

#include <yr/core/string_interner.h>
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

    [[nodiscard]] core::ObjectID actorId(const BoxCollider& collider) noexcept {
      return collider.parent() != nullptr ? collider.parent()->id() : collider.id();
    }

  } // namespace

  bool overlaps(const Aabb& lhs, const Aabb& rhs) noexcept {
    return lhs.minimum.x <= rhs.maximum.x && lhs.maximum.x >= rhs.minimum.x && lhs.minimum.y <= rhs.maximum.y &&
           lhs.maximum.y >= rhs.minimum.y && lhs.minimum.z <= rhs.maximum.z && lhs.maximum.z >= rhs.minimum.z;
  }

  void BoxCollider::setHalfExtents(const math::Vec3& halfExtents) noexcept {
    halfExtents_ = math::Vec3{std::abs(halfExtents.x), std::abs(halfExtents.y), std::abs(halfExtents.z)};
  }

  void BoxCollider::setTag(std::string_view tag) {
    tag_ = core::globalStringInterner().intern(tag);
  }
  void BoxCollider::setFilterTag(std::string_view tag) {
    filterTag_ = core::globalStringInterner().intern(tag);
  }

  Aabb makeAabb(const math::Mat4& world, const math::Vec3& halfExtents) noexcept {
    Aabb bounds;
    bool initialized = false;
    for (int corner = 0; corner < 8; ++corner) {
      const math::Vec3 local{(corner & 1) != 0 ? halfExtents.x : -halfExtents.x,
                             (corner & 2) != 0 ? halfExtents.y : -halfExtents.y,
                             (corner & 4) != 0 ? halfExtents.z : -halfExtents.z};
      const math::Vec3 point = world.transformPoint(local);
      if (!initialized) {
        bounds.minimum = point;
        bounds.maximum = point;
        initialized = true;
      } else {
        bounds.minimum.x = std::min(bounds.minimum.x, point.x);
        bounds.minimum.y = std::min(bounds.minimum.y, point.y);
        bounds.minimum.z = std::min(bounds.minimum.z, point.z);
        bounds.maximum.x = std::max(bounds.maximum.x, point.x);
        bounds.maximum.y = std::max(bounds.maximum.y, point.y);
        bounds.maximum.z = std::max(bounds.maximum.z, point.z);
      }
    }
    return bounds;
  }

  Aabb BoxCollider::worldAabb() const noexcept {
    const math::Mat4 world = parent() != nullptr ? parent()->globalTransform() : math::Mat4{};
    return makeAabb(world, halfExtents_);
  }

  void BoxCollider::onUpdate(float /*deltaTime*/) {
    if (!trigger_ || !enabled_ || tree() == nullptr) {
      // 传感器被关闭时补发离开，避免下游状态停在“重叠中”。
      for (const core::ObjectID previous : overlaps_) {
        emitSignal(intern("exited"), obj::Variant(previous));
      }
      overlaps_.clear();
      return;
    }

    std::vector<const BoxCollider*> colliders;
    detail::collectColliders(*tree()->root(), colliders);

    const Aabb self = worldAabb();
    std::unordered_set<core::ObjectID> current;
    for (const BoxCollider* other : colliders) {
      if (other == this || !other->enabled()) {
        continue;
      }
      if (filterTag_.valid() && other->tag() != filterTag_) {
        continue;
      }
      if (overlaps(self, other->worldAabb())) {
        current.insert(actorId(*other));
      }
    }

    for (const core::ObjectID previous : overlaps_) {
      if (!current.contains(previous)) {
        emitSignal(intern("exited"), obj::Variant(previous));
      }
    }
    for (const core::ObjectID now : current) {
      if (!overlaps_.contains(now)) {
        emitSignal(intern("entered"), obj::Variant(now));
      }
    }
    overlaps_ = std::move(current);
  }

  void BoxCollider::onExitTree() {
    overlaps_.clear();
  }

  const obj::ClassInfo& boxColliderClassInfo() noexcept {
    using namespace obj;

    static const PropertyInfo kProperties[] = {
        {.name = intern("half_extents"),
         .type = Variant::Type::kVec3,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const BoxCollider*>(object)->halfExtents());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto extents = value.tryAs<math::Vec3>();
           if (!extents.has_value()) {
             return false;
           }
           static_cast<BoxCollider*>(object)->setHalfExtents(*extents);
           return true;
         }},
        {.name = intern("tag"),
         .type = Variant::Type::kString,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(std::string(static_cast<const BoxCollider*>(object)->tag().view()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto text = value.tryAs<std::string>();
           if (!text.has_value()) {
             return false;
           }
           static_cast<BoxCollider*>(object)->setTag(*text);
           return true;
         }},
        {.name = intern("filter_tag"),
         .type = Variant::Type::kString,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(std::string(static_cast<const BoxCollider*>(object)->filterTag().view()));
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto text = value.tryAs<std::string>();
           if (!text.has_value()) {
             return false;
           }
           static_cast<BoxCollider*>(object)->setFilterTag(*text);
           return true;
         }},
        {.name = intern("solid"),
         .type = Variant::Type::kBool,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const BoxCollider*>(object)->solid());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto flag = value.tryAs<bool>();
           if (!flag.has_value()) {
             return false;
           }
           static_cast<BoxCollider*>(object)->setSolid(*flag);
           return true;
         }},
        {.name = intern("trigger"),
         .type = Variant::Type::kBool,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const BoxCollider*>(object)->trigger());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto flag = value.tryAs<bool>();
           if (!flag.has_value()) {
             return false;
           }
           static_cast<BoxCollider*>(object)->setTrigger(*flag);
           return true;
         }},
        {.name = intern("enabled"),
         .type = Variant::Type::kBool,
         .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
         .getter = [](const Object* object) -> Variant {
           return Variant(static_cast<const BoxCollider*>(object)->enabled());
         },
         .setter = [](Object* object, const Variant& value) -> bool {
           const auto flag = value.tryAs<bool>();
           if (!flag.has_value()) {
             return false;
           }
           static_cast<BoxCollider*>(object)->setEnabled(*flag);
           return true;
         }},
    };

    static const ClassInfo info(
        intern("BoxCollider"), &scene::nodeClassInfo(), kProperties,
        +[]() -> std::unique_ptr<Object> { return std::make_unique<BoxCollider>(); });
    return info;
  }

  const obj::ClassInfo* BoxCollider::classInfo() const noexcept {
    return &boxColliderClassInfo();
  }

} // namespace yr::gameplay