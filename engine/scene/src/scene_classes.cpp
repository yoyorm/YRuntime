#include "yr/object/object.h"
#include <yr/scene/scene_classes.h>

#include <cstdint>
#include <memory>

#include <yr/core/string_interner.h>
#include <yr/math/vec3.h>
#include <yr/object/class_db.h>
#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/register_core_classes.h>
#include <yr/object/variant.h>
#include <yr/scene/node.h>

namespace yr::scene {

  namespace {

    const yr::obj::ClassInfo& buildNodeClassInfo() noexcept {
      using namespace yr::obj;

      // 让 Node 可实例化并暴露 position 属性：
      auto factory = []() -> Object* { return std::make_unique<Node>().release(); };
      static const PropertyInfo kProperties[] = {
          PropertyInfo{
              .name = core::globalStringInterner().intern("position"),
              .type = Variant::Type::kVec3,
              .flags = static_cast<std::uint32_t>(kSerialized | kEditable),
              .getter = [](const Object* o) -> Variant { return Variant(static_cast<const Node*>(o)->position()); },
              .setter = [](Object* o, const Variant& v) -> bool {
                static_cast<Node*>(o)->setPosition(v.as<yr::math::Vec3>());
                return true;
              },
          },
      };

      static const ClassInfo info(core::globalStringInterner().intern("Node"), &objectClassInfo(), kProperties,
                                  factory);
      return info;
    }

  } // namespace

  const yr::obj::ClassInfo& nodeClassInfo() noexcept {
    return buildNodeClassInfo();
  }

  const yr::obj::ClassInfo* Node::classInfo() const noexcept {
    return &nodeClassInfo();
  }

  void register_scene_classes() {
    static bool registered = false;
    if (registered) {
      return;
    }
    obj::register_core_classes();
    obj::globalClassDB().registerClass(nodeClassInfo());
    registered = true;
  }

} // namespace yr::scene