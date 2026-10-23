#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include <yr/math/vec3.h>

namespace yr::math {

  // 4x4 矩阵（Transform 所需子集）。约定（与 M5 RenderSnapshot 契约冻结一致）：
  //   1. 存储 column-major：m_[col * 4 + row]，m_[0..3] 是第 0 列
  //   2. 向量为列向量，v' = M * v
  //   3. 右手坐标系，Y-up（glTF 标准）
  //   4. 深度范围 [0, 1]（Vulkan NDC）
  // 值语义、constexpr 友好、无动态分配；默认构造为 identity（矩阵惯例）。
  struct Mat4 {
    // 原始存储（column-major）：index = col * 4 + row。
    // 例：仿射平移矩阵的平移量在第 3 列，即 m_[12], m_[13], m_[14]。
    std::array<float, 16> m_;

    constexpr Mat4() noexcept
        : m_{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F} {}

    // identity 工厂
    [[nodiscard]] static constexpr Mat4 identity() noexcept { return Mat4{}; }

    // 透视投影：右手、深度 [0, 1]（Vulkan NDC），fovY 为垂直视场角（弧度）。
    [[nodiscard]] static Mat4 perspective(float fovYRadians, float aspect, float nearPlane, float farPlane) noexcept {
      Mat4 result;
      for (float& value : result.m_) {
        value = 0.0F;
      }
      const float f = 1.0F / std::tan(fovYRadians * 0.5F);
      result[0] = f / aspect;
      result[5] = f;
      result[10] = farPlane / (nearPlane - farPlane);
      result[11] = -1.0F;
      result[14] = (farPlane * nearPlane) / (nearPlane - farPlane);
      result[15] = 0.0F;
      return result;
    }

    // 视图矩阵：右手 lookAt，eye 为相机位置。
    [[nodiscard]] static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept {
      const Vec3 f = (target - eye).normalized();
      const Vec3 s = cross(f, up).normalized();
      const Vec3 u = cross(s, f);

      Mat4 result;
      for (float& value : result.m_) {
        value = 0.0F;
      }
      result[0] = s.x;
      result[4] = s.y;
      result[8] = s.z;
      result[12] = -dot(s, eye);
      result[1] = u.x;
      result[5] = u.y;
      result[9] = u.z;
      result[13] = -dot(u, eye);
      result[2] = -f.x;
      result[6] = -f.y;
      result[10] = -f.z;
      result[14] = dot(f, eye);
      result[15] = 1.0F;
      return result;
    }

    // 平坦索引访问原始存储：index = col * 4 + row。
    [[nodiscard]] constexpr float& operator[](std::size_t index) noexcept { return m_[index]; }
    [[nodiscard]] constexpr const float& operator[](std::size_t index) const noexcept { return m_[index]; }

    // 精确浮点比较（与 Vec3 的 == 风格一致，不做 epsilon 比较）。
    friend constexpr bool operator==(const Mat4& lhs, const Mat4& rhs) noexcept = default;
    friend constexpr bool operator!=(const Mat4& lhs, const Mat4& rhs) noexcept { return !(lhs == rhs); }

    // 转置（返回新矩阵，不修改自身）。
    [[nodiscard]] constexpr Mat4 transpose() const noexcept {
      Mat4 result;
      for (std::size_t col = 0; col < 4; ++col) {
        for (std::size_t row = 0; row < 4; ++row) {
          result[col * 4 + row] = m_[row * 4 + col];
        }
      }
      return result;
    }

    // 变换点：w = 1，包含平移。本阶段无投影，不做透视除法（直接返回 xyz）。
    [[nodiscard]] constexpr Vec3 transformPoint(Vec3 point) const noexcept {
      return Vec3{m_[0] * point.x + m_[4] * point.y + m_[8] * point.z + m_[12],
                  m_[1] * point.x + m_[5] * point.y + m_[9] * point.z + m_[13],
                  m_[2] * point.x + m_[6] * point.y + m_[10] * point.z + m_[14]};
    }

    // 变换方向：w = 0，不受平移影响。
    [[nodiscard]] constexpr Vec3 transformVector(Vec3 direction) const noexcept {
      return Vec3{m_[0] * direction.x + m_[4] * direction.y + m_[8] * direction.z,
                  m_[1] * direction.x + m_[5] * direction.y + m_[9] * direction.z,
                  m_[2] * direction.x + m_[6] * direction.y + m_[10] * direction.z};
    }
  };

  // 矩阵乘法：先应用 rhs 再应用 lhs，即 v' = lhs * (rhs * v)。
  [[nodiscard]] constexpr Mat4 operator*(const Mat4& lhs, const Mat4& rhs) noexcept {
    Mat4 result;
    for (std::size_t col = 0; col < 4; ++col) {
      for (std::size_t row = 0; row < 4; ++row) {
        float sum = 0.0F;
        for (std::size_t k = 0; k < 4; ++k) {
          sum += lhs[k * 4 + row] * rhs[col * 4 + k];
        }
        result[col * 4 + row] = sum;
      }
    }
    return result;
  }

  // TODO(M?)：inverse、ortho、TRS 合成与分解。

} // namespace yr::math