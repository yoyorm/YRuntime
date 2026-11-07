#pragma once

#include <yr/math/quat.h>

namespace yr::math {

  struct Transform {
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{1.0F, 1.0F, 1.0F};

    friend constexpr bool operator==(Transform, Transform) noexcept = default;
  };

  // Column-major, column-vector convention: translate, then rotate, then scale.
  [[nodiscard]] inline Mat4 toMat4(const Transform& transform) noexcept {
    Mat4 translation;
    translation[12] = transform.position.x;
    translation[13] = transform.position.y;
    translation[14] = transform.position.z;

    Mat4 scale;
    scale[0] = transform.scale.x;
    scale[5] = transform.scale.y;
    scale[10] = transform.scale.z;
    return translation * transform.rotation.toMat4() * scale;
  }

} // namespace yr::math
