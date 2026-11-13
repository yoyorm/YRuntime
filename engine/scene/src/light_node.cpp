#include <yr/scene/light_node.h>

#include <memory>

#include <yr/core/string_interner.h>
#include <yr/object/class_info.h>
#include <yr/scene/scene_classes.h>

namespace yr::scene {

  namespace {

    // 短于此长度的方向视为无方向；零向量是安全输入，回退到默认朝向。
    constexpr float kMinDirectionLength = 1e-6F;

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
    auto factory = []() -> obj::Object* { return std::make_unique<LightNode>().release(); };
    static const obj::ClassInfo info(core::globalStringInterner().intern("LightNode"), &nodeClassInfo(), {}, factory);
    return info;
  }

  const obj::ClassInfo* LightNode::classInfo() const noexcept {
    return &lightNodeClassInfo();
  }

} // namespace yr::scene
