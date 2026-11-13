#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <yr/math/mat4.h>
#include <yr/math/vec3.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

using yr::math::Mat4;
using yr::math::Vec3;

static_assert(std::is_trivially_copyable_v<Mat4>);
static_assert(std::is_default_constructible_v<Mat4>);
static_assert(sizeof(Mat4) == 64);

namespace {

  // 按 column-major 平坦索引填充（与 Mat4 存储约定一致）。
  Mat4 fromArray(const std::array<float, 16>& values) {
    Mat4 result;
    for (std::size_t i = 0; i < 16; ++i) {
      result[i] = values[i];
    }
    return result;
  }

  // 手写平移矩阵：第 3 列为 (tx, ty, tz, 1)，column-major 下即索引 12/13/14。
  Mat4 translation(float tx, float ty, float tz) {
    Mat4 result;
    result[12] = tx;
    result[13] = ty;
    result[14] = tz;
    return result;
  }

} // namespace

TEST_CASE("mat4: 默认构造为 identity", "[core][mat4]") {
  const Mat4 value;

  CHECK(value == Mat4::identity());
  CHECK(value[0] == 1.0F);
  CHECK(value[5] == 1.0F);
  CHECK(value[10] == 1.0F);
  CHECK(value[15] == 1.0F);
  for (std::size_t i = 0; i < 16; ++i) {
    if (i != 0 && i != 5 && i != 10 && i != 15) {
      CHECK(value[i] == 0.0F);
    }
  }
}

TEST_CASE("mat4: identity 乘任何矩阵不变", "[core][mat4]") {
  const Mat4 value = fromArray(
      {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F, 10.0F, 11.0F, 12.0F, 13.0F, 14.0F, 15.0F, 16.0F});

  CHECK(Mat4::identity() * value == value);
  CHECK(value * Mat4::identity() == value);
}

TEST_CASE("mat4: 乘法满足结合律", "[core][mat4]") {
  // 全部用小整数，保证 float 精确可表示，operator== 精确比较才有意义。
  const Mat4 a =
      fromArray({1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0F, 0.0F, 0.0F, 0.0F, 3.0F, 0.0F, 1.0F, 2.0F, 3.0F, 1.0F});
  const Mat4 b =
      fromArray({0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 4.0F, 5.0F, 6.0F, 1.0F});
  const Mat4 c =
      fromArray({1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0F, 2.0F, 3.0F, 4.0F, 1.0F});

  CHECK((a * b) * c == a * (b * c));
}

TEST_CASE("mat4: 矩阵乘向量（点与方向的平移差异）", "[core][mat4]") {
  const Mat4 t = translation(1.0F, 2.0F, 3.0F);

  CHECK(t.transformPoint(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(1.0F, 2.0F, 3.0F));
  CHECK(t.transformPoint(Vec3(1.0F, 1.0F, 1.0F)) == Vec3(2.0F, 3.0F, 4.0F));
  // 方向向量 w=0，不受平移影响。
  CHECK(t.transformVector(Vec3(1.0F, 1.0F, 1.0F)) == Vec3(1.0F, 1.0F, 1.0F));
  CHECK(t.transformVector(Vec3(0.0F, 0.0F, 0.0F)) == Vec3(0.0F, 0.0F, 0.0F));
}

TEST_CASE("mat4: 乘法语义为先 B 后 A", "[core][mat4]") {
  const Mat4 a = translation(10.0F, 0.0F, 0.0F);
  const Mat4 b = translation(0.0F, 20.0F, 0.0F);
  const Vec3 v(1.0F, 2.0F, 3.0F);

  CHECK((a * b).transformPoint(v) == a.transformPoint(b.transformPoint(v)));
  CHECK((a * b).transformPoint(v) == Vec3(11.0F, 22.0F, 3.0F));
}

TEST_CASE("mat4: 转置（非对称矩阵手算期望）", "[core][mat4]") {
  const Mat4 value =
      fromArray({0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F, 10.0F, 11.0F, 12.0F, 13.0F, 14.0F, 15.0F});

  // 手算：transpose[col * 4 + row] = value[row * 4 + col]
  const std::array<float, 16> expected = {0.0F, 4.0F, 8.0F,  12.0F, 1.0F, 5.0F, 9.0F,  13.0F,
                                          2.0F, 6.0F, 10.0F, 14.0F, 3.0F, 7.0F, 11.0F, 15.0F};
  const Mat4 transposed = value.transpose();

  for (std::size_t i = 0; i < 16; ++i) {
    CHECK(transposed[i] == expected[i]);
  }
  // 转置转置回自身。
  CHECK(transposed.transpose() == value);
}

TEST_CASE("mat4: column-major 存储布局锁定", "[core][mat4]") {
  // 若误把存储改成 row-major，平移量会落到 m[3]/m[7]/m[11]，以下断言全部失败。
  const Mat4 t = translation(5.0F, 6.0F, 7.0F);

  CHECK(t[12] == 5.0F);
  CHECK(t[13] == 6.0F);
  CHECK(t[14] == 7.0F);
  CHECK(t[15] == 1.0F);

  CHECK(t[0] == 1.0F);
  CHECK(t[5] == 1.0F);
  CHECK(t[10] == 1.0F);

  // 第 0/1/2 列的 w 分量必须为 0（column-major 下索引 3/7/11）。
  CHECK(t[3] == 0.0F);
  CHECK(t[7] == 0.0F);
  CHECK(t[11] == 0.0F);
}

TEST_CASE("mat4: 相等与不等", "[core][mat4]") {
  const Mat4 a = translation(1.0F, 2.0F, 3.0F);
  const Mat4 b = translation(1.0F, 2.0F, 3.0F);
  const Mat4 c = translation(1.0F, 2.0F, 4.0F);

  CHECK(a == b);
  CHECK(a != c);
  CHECK(a != Mat4::identity());
}

TEST_CASE("mat4: perspective 缩放与 Vulkan 深度区间", "[core][mat4]") {
  constexpr float fovY = 1.0F;
  constexpr float aspect = 16.0F / 9.0F;
  constexpr float nearPlane = 0.1F;
  constexpr float farPlane = 100.0F;
  const Mat4 projection = Mat4::perspective(fovY, aspect, nearPlane, farPlane);

  const float f = 1.0F / std::tan(fovY * 0.5F);
  CHECK(projection[0] == Catch::Approx(f / aspect));
  CHECK(projection[5] == Catch::Approx(f));
  CHECK(projection[11] == -1.0F);
  CHECK(projection[15] == 0.0F);

  // 透视除法后：near 平面映射到 0，far 平面映射到 1。
  const auto ndcDepth = [&projection](float eyeZ) {
    const Vec3 clip = projection.transformPoint(Vec3{0.0F, 0.0F, eyeZ});
    const float w = -eyeZ; // m[11] = -1，且 x/y 为 0。
    return clip.z / w;
  };
  CHECK(ndcDepth(-nearPlane) == Catch::Approx(0.0F).margin(1e-6F));
  CHECK(ndcDepth(-farPlane) == Catch::Approx(1.0F).margin(1e-6F));
}

TEST_CASE("mat4: orthographic maps view space to Vulkan NDC", "[core][mat4]") {
  constexpr float left = -2.0F;
  constexpr float right = 2.0F;
  constexpr float bottom = -1.0F;
  constexpr float top = 1.0F;
  constexpr float nearPlane = 0.1F;
  constexpr float farPlane = 100.0F;
  const Mat4 projection = Mat4::orthographic(left, right, bottom, top, nearPlane, farPlane);

  // 正交投影 w 恒为 1，transformPoint 的结果即 NDC。
  CHECK(projection[15] == 1.0F);
  CHECK(projection[3] == 0.0F);
  CHECK(projection[7] == 0.0F);
  CHECK(projection[11] == 0.0F);

  const Vec3 nearCenter = projection.transformPoint(Vec3{0.0F, 0.0F, -nearPlane});
  CHECK(nearCenter.x == Catch::Approx(0.0F).margin(1e-6F));
  CHECK(nearCenter.y == Catch::Approx(0.0F).margin(1e-6F));
  CHECK(nearCenter.z == Catch::Approx(0.0F).margin(1e-6F));

  const Vec3 farCenter = projection.transformPoint(Vec3{0.0F, 0.0F, -farPlane});
  CHECK(farCenter.x == Catch::Approx(0.0F).margin(1e-6F));
  CHECK(farCenter.y == Catch::Approx(0.0F).margin(1e-6F));
  CHECK(farCenter.z == Catch::Approx(1.0F).margin(1e-6F));

  // 左右 / 上下边界映射到 ±1，与深度无关。
  CHECK(projection.transformPoint(Vec3{left, 0.0F, -nearPlane}).x == Catch::Approx(-1.0F).margin(1e-6F));
  CHECK(projection.transformPoint(Vec3{right, 0.0F, -nearPlane}).x == Catch::Approx(1.0F).margin(1e-6F));
  CHECK(projection.transformPoint(Vec3{0.0F, bottom, -nearPlane}).y == Catch::Approx(-1.0F).margin(1e-6F));
  CHECK(projection.transformPoint(Vec3{0.0F, top, -nearPlane}).y == Catch::Approx(1.0F).margin(1e-6F));

  // 与 perspective 一致的深度约定：near → 0，far → 1（Vulkan [0,1]）。
  const Mat4 perspective = Mat4::perspective(1.0F, 16.0F / 9.0F, nearPlane, farPlane);
  const auto perspectiveDepth = [&perspective](float eyeZ) {
    const Vec3 clip = perspective.transformPoint(Vec3{0.0F, 0.0F, eyeZ});
    return clip.z / -eyeZ;
  };
  CHECK(perspectiveDepth(-nearPlane) == Catch::Approx(nearCenter.z).margin(1e-6F));
  CHECK(perspectiveDepth(-farPlane) == Catch::Approx(farCenter.z).margin(1e-6F));
}

TEST_CASE("mat4: lookAt 从 +Z 看原点等于平移 -Z", "[core][mat4]") {
  const Vec3 eye{0.0F, 0.0F, 4.0F};
  const Mat4 view = Mat4::lookAt(eye, Vec3{0.0F, 0.0F, 0.0F}, Vec3{0.0F, 1.0F, 0.0F});

  const Mat4 expected = translation(0.0F, 0.0F, -4.0F);
  for (std::size_t i = 0; i < 16; ++i) {
    CHECK(view[i] == expected[i]);
  }

  // eye 变换后落在原点（相机空间原点）。
  const Vec3 transformedEye = view.transformPoint(eye);
  CHECK(transformedEye.x == Catch::Approx(0.0F).margin(1e-6F));
  CHECK(transformedEye.y == Catch::Approx(0.0F).margin(1e-6F));
  CHECK(transformedEye.z == Catch::Approx(0.0F).margin(1e-6F));
}