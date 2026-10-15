#include <catch2/catch_test_macros.hpp>

#include <yr/math/mat4.h>
#include <yr/math/vec3.h>

#include <array>
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