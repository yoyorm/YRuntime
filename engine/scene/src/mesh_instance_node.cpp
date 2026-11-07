#include <yr/scene/mesh_instance_node.h>

#include <memory>

#include <yr/core/string_interner.h>
#include <yr/object/class_info.h>
#include <yr/scene/scene_classes.h>

namespace yr::scene {

  const obj::ClassInfo& meshInstanceNodeClassInfo() noexcept {
    auto factory = []() -> obj::Object* { return std::make_unique<MeshInstanceNode>().release(); };
    static const obj::ClassInfo info(core::globalStringInterner().intern("MeshInstanceNode"), &nodeClassInfo(), {},
                                     factory);
    return info;
  }

  const obj::ClassInfo* MeshInstanceNode::classInfo() const noexcept {
    return &meshInstanceNodeClassInfo();
  }

} // namespace yr::scene
