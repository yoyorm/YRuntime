#pragma once

#include <string_view>
#include <unordered_set>

#include <yr/core/object_id.h>
#include <yr/core/string_id.h>
#include <yr/math/mat4.h>
#include <yr/math/vec3.h>
#include <yr/scene/node.h>

namespace yr::gameplay {

  struct Aabb {
    math::Vec3 minimum{};
    math::Vec3 maximum{};
  };

  [[nodiscard]] bool overlaps(const Aabb& lhs, const Aabb& rhs) noexcept;

  // 把局部半尺寸按世界矩阵展开成 AABB（取 8 个角点）。MoveMotor 用它预测下一格。
  [[nodiscard]] Aabb makeAabb(const math::Mat4& world, const math::Vec3& halfExtents) noexcept;

  // solid 用于移动阻挡；trigger 用于 overlap 信号；tag/filter_tag 用于筛选。
  // AABB 由 parent() 的 globalTransform（上一帧缓存）展开；trigger 只在进入/离开变化时发信号，
  // 禁用或离树时补发 exited / 清空状态。
  class BoxCollider final : public scene::Node {
  public:
    void onUpdate(float deltaTime) override;
    void onExitTree() override;

    [[nodiscard]] Aabb worldAabb() const noexcept;

    [[nodiscard]] const math::Vec3& halfExtents() const noexcept { return halfExtents_; }
    void setHalfExtents(const math::Vec3& halfExtents) noexcept;

    [[nodiscard]] core::StringId tag() const noexcept { return tag_; }
    void setTag(std::string_view tag);
    [[nodiscard]] core::StringId filterTag() const noexcept { return filterTag_; }
    void setFilterTag(std::string_view tag);

    [[nodiscard]] bool solid() const noexcept { return solid_; }
    void setSolid(bool solid) noexcept { solid_ = solid; }
    [[nodiscard]] bool trigger() const noexcept { return trigger_; }
    void setTrigger(bool trigger) noexcept { trigger_ = trigger; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }

    [[nodiscard]] const obj::ClassInfo* classInfo() const noexcept override;

  private:
    math::Vec3 halfExtents_{0.5F, 0.5F, 0.5F};
    core::StringId tag_{};
    core::StringId filterTag_{};
    bool solid_ = false;
    bool trigger_ = false;
    bool enabled_ = true;
    std::unordered_set<core::ObjectID> overlaps_;
  };

  [[nodiscard]] const obj::ClassInfo& boxColliderClassInfo() noexcept;

} // namespace yr::gameplay