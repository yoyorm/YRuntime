#pragma once

#include <cmath>

#include <yr/core/assert.h>
#include <yr/math/mat4.h>

namespace yr::math {

  struct Quat {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;

    [[nodiscard]] static constexpr Quat identity() noexcept { return Quat{}; }

    [[nodiscard]] static Quat fromAxisAngle(const Vec3& axis, float radians) noexcept {
      const float length = axis.length();
      YR_ASSERT_MSG(length > 0.0F, "Quat::fromAxisAngle: zero axis");
      if (length <= 0.0F) {
        return identity();
      }

      const float halfAngle = radians * 0.5F;
      const float sine = std::sin(halfAngle) / length;
      return Quat{axis.x * sine, axis.y * sine, axis.z * sine, std::cos(halfAngle)};
    }

    [[nodiscard]] static Quat fromRotationMatrix(const Mat4& matrix) noexcept {
      const float m00 = matrix[0];
      const float m11 = matrix[5];
      const float m22 = matrix[10];
      const float trace = m00 + m11 + m22;
      Quat result;

      if (trace > 0.0F) {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        result.w = 0.25F * s;
        result.x = (matrix[6] - matrix[9]) / s;
        result.y = (matrix[8] - matrix[2]) / s;
        result.z = (matrix[1] - matrix[4]) / s;
      } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0F + m00 - m11 - m22) * 2.0F;
        result.w = (matrix[6] - matrix[9]) / s;
        result.x = 0.25F * s;
        result.y = (matrix[4] + matrix[1]) / s;
        result.z = (matrix[8] + matrix[2]) / s;
      } else if (m11 > m22) {
        const float s = std::sqrt(1.0F + m11 - m00 - m22) * 2.0F;
        result.w = (matrix[8] - matrix[2]) / s;
        result.x = (matrix[4] + matrix[1]) / s;
        result.y = 0.25F * s;
        result.z = (matrix[9] + matrix[6]) / s;
      } else {
        const float s = std::sqrt(1.0F + m22 - m00 - m11) * 2.0F;
        result.w = (matrix[1] - matrix[4]) / s;
        result.x = (matrix[8] + matrix[2]) / s;
        result.y = (matrix[9] + matrix[6]) / s;
        result.z = 0.25F * s;
      }
      return result.normalized();
    }

    [[nodiscard]] Quat normalized() const noexcept {
      const float length = std::sqrt(x * x + y * y + z * z + w * w);
      YR_ASSERT_MSG(length > 0.0F, "Quat::normalized: zero length");
      if (length <= 0.0F) {
        return identity();
      }
      return Quat{x / length, y / length, z / length, w / length};
    }

    [[nodiscard]] constexpr Quat conjugate() const noexcept { return Quat{-x, -y, -z, w}; }

    [[nodiscard]] Vec3 rotate(const Vec3& value) const noexcept {
      const Vec3 vector{x, y, z};
      const Vec3 twiceCross = 2.0F * cross(vector, value);
      return value + w * twiceCross + cross(vector, twiceCross);
    }

    [[nodiscard]] Mat4 toMat4() const noexcept {
      const float xx = x * x;
      const float yy = y * y;
      const float zz = z * z;
      const float xy = x * y;
      const float xz = x * z;
      const float yz = y * z;
      const float xw = x * w;
      const float yw = y * w;
      const float zw = z * w;

      Mat4 result;
      result[0] = 1.0F - 2.0F * (yy + zz);
      result[1] = 2.0F * (xy + zw);
      result[2] = 2.0F * (xz - yw);
      result[4] = 2.0F * (xy - zw);
      result[5] = 1.0F - 2.0F * (xx + zz);
      result[6] = 2.0F * (yz + xw);
      result[8] = 2.0F * (xz + yw);
      result[9] = 2.0F * (yz - xw);
      result[10] = 1.0F - 2.0F * (xx + yy);
      return result;
    }

    friend constexpr bool operator==(Quat, Quat) noexcept = default;
  };

  // lhs * rhs applies rhs first, matching Mat4 multiplication.
  [[nodiscard]] inline Quat operator*(const Quat& lhs, const Quat& rhs) noexcept {
    return Quat{lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
                lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
                lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z};
  }

} // namespace yr::math
