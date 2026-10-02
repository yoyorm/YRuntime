#pragma once

#include <cmath>

#include <yr/core/assert.h>

namespace yr::obj {

  // 最小 3D 向量：值语义、constexpr 友好、无动态分配。
  // 目前只服务 Variant 的 kVec3。等 M4（Transform）/ M9（快照）真正需要时，
  // 按 D4 迁到 yr/core（render_iface 只能依赖 core），届时是一次机械搬迁。
  struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    constexpr Vec3() noexcept = default;
    constexpr Vec3(float x_value, float y_value, float z_value) noexcept : x(x_value), y(y_value), z(z_value) {}

    friend constexpr bool operator==(Vec3, Vec3) noexcept = default;
    friend constexpr bool operator!=(Vec3 lhs, Vec3 rhs) noexcept { return !(lhs == rhs); }

    constexpr Vec3& operator+=(Vec3 other) noexcept {
      x += other.x;
      y += other.y;
      z += other.z;
      return *this;
    }

    constexpr Vec3& operator-=(Vec3 other) noexcept {
      x -= other.x;
      y -= other.y;
      z -= other.z;
      return *this;
    }

    constexpr Vec3& operator*=(float scalar) noexcept {
      x *= scalar;
      y *= scalar;
      z *= scalar;
      return *this;
    }

    constexpr Vec3& operator/=(float scalar) noexcept {
      x /= scalar;
      y /= scalar;
      z /= scalar;
      return *this;
    }

    [[nodiscard]] constexpr float lengthSquared() const noexcept { return x * x + y * y + z * z; }

    [[nodiscard]] float length() const noexcept { return std::sqrt(lengthSquared()); }

    [[nodiscard]] Vec3 normalized() const noexcept {
      const float len = length();
      YR_ASSERT_MSG(len > 0.0F, "Vec3::normalized: zero vector");
      return Vec3{x / len, y / len, z / len};
    }
  };

  [[nodiscard]] constexpr Vec3 operator+(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
  }

  [[nodiscard]] constexpr Vec3 operator-(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
  }

  [[nodiscard]] constexpr Vec3 operator-(Vec3 value) noexcept {
    return Vec3{-value.x, -value.y, -value.z};
  }

  [[nodiscard]] constexpr Vec3 operator*(Vec3 value, float scalar) noexcept {
    return Vec3{value.x * scalar, value.y * scalar, value.z * scalar};
  }

  [[nodiscard]] constexpr Vec3 operator*(float scalar, Vec3 value) noexcept {
    return value * scalar;
  }

  [[nodiscard]] constexpr Vec3 operator/(Vec3 value, float scalar) noexcept {
    return Vec3{value.x / scalar, value.y / scalar, value.z / scalar};
  }

  [[nodiscard]] constexpr float dot(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
  }

  [[nodiscard]] constexpr Vec3 cross(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x};
  }

} // namespace yr::obj