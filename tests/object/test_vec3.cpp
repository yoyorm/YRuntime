#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <yr/object/vec3.h>

#include <type_traits>

using yr::obj::cross;
using yr::obj::dot;
using yr::obj::Vec3;

static_assert(std::is_trivially_copyable_v<Vec3>);
static_assert(std::is_default_constructible_v<Vec3>);

TEST_CASE("vec3: 默认构造为零向量", "[object][vec3]") {
  const Vec3 value;

  CHECK(value.x == 0.0F);
  CHECK(value.y == 0.0F);
  CHECK(value.z == 0.0F);
  CHECK(value == Vec3{});
}

TEST_CASE("vec3: 构造与相等", "[object][vec3]") {
  const Vec3 value(1.0F, 2.0F, 3.0F);

  CHECK(value == Vec3(1.0F, 2.0F, 3.0F));
  CHECK(value != Vec3(1.0F, 2.0F, 4.0F));
}

TEST_CASE("vec3: 加减与取负", "[object][vec3]") {
  const Vec3 a(1.0F, 2.0F, 3.0F);
  const Vec3 b(4.0F, 5.0F, 6.0F);

  CHECK(a + b == Vec3(5.0F, 7.0F, 9.0F));
  CHECK(b - a == Vec3(3.0F, 3.0F, 3.0F));
  CHECK(-a == Vec3(-1.0F, -2.0F, -3.0F));
}

TEST_CASE("vec3: 缩放与除法", "[object][vec3]") {
  const Vec3 a(1.0F, 2.0F, 3.0F);

  CHECK(a * 2.0F == Vec3(2.0F, 4.0F, 6.0F));
  CHECK(2.0F * a == Vec3(2.0F, 4.0F, 6.0F));
  CHECK(a / 2.0F == Vec3(0.5F, 1.0F, 1.5F));
}

TEST_CASE("vec3: 复合赋值", "[object][vec3]") {
  Vec3 value(1.0F, 2.0F, 3.0F);

  value += Vec3(1.0F, 1.0F, 1.0F);
  CHECK(value == Vec3(2.0F, 3.0F, 4.0F));

  value -= Vec3(1.0F, 1.0F, 1.0F);
  CHECK(value == Vec3(1.0F, 2.0F, 3.0F));

  value *= 2.0F;
  CHECK(value == Vec3(2.0F, 4.0F, 6.0F));

  value /= 2.0F;
  CHECK(value == Vec3(1.0F, 2.0F, 3.0F));
}

TEST_CASE("vec3: dot 与 cross", "[object][vec3]") {
  CHECK(dot(Vec3(1.0F, 2.0F, 3.0F), Vec3(4.0F, 5.0F, 6.0F)) == 32.0F);
  CHECK(dot(Vec3(1.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F)) == 0.0F);

  CHECK(cross(Vec3(1.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F)) == Vec3(0.0F, 0.0F, 1.0F));
  CHECK(cross(Vec3(0.0F, 1.0F, 0.0F), Vec3(1.0F, 0.0F, 0.0F)) == Vec3(0.0F, 0.0F, -1.0F));
}

TEST_CASE("vec3: 长度与归一化", "[object][vec3]") {
  CHECK(Vec3(3.0F, 4.0F, 0.0F).lengthSquared() == 25.0F);
  CHECK(Vec3(3.0F, 4.0F, 0.0F).length() == Catch::Approx(5.0F));

  const Vec3 unit = Vec3(3.0F, 4.0F, 0.0F).normalized();
  CHECK(unit.x == Catch::Approx(0.6F));
  CHECK(unit.y == Catch::Approx(0.8F));
  CHECK(unit.z == Catch::Approx(0.0F));
  CHECK(unit.length() == Catch::Approx(1.0F));
}