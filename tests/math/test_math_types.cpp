#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math_test_support.h"

#include <array>
#include <cmath>
#include <numbers>
#include <type_traits>

#include <yr/math/quat.h>
#include <yr/math/transform.h>
#include <yr/math/vec3.h>

using namespace yr_test;

using yr::math::cross;
using yr::math::dot;
using yr::math::Mat4;
using yr::math::Quat;
using yr::math::Transform;
using yr::math::Vec3;

static_assert(std::is_trivially_copyable_v<Vec3>);
static_assert(std::is_default_constructible_v<Vec3>);

TEST_CASE("vec3: 默认构造为零向量", "[core][vec3]") {
  const Vec3 value;

  CHECK(value.x == 0.0F);
  CHECK(value.y == 0.0F);
  CHECK(value.z == 0.0F);
  CHECK(value == Vec3{});
}

TEST_CASE("vec3: 构造与相等", "[core][vec3]") {
  const Vec3 value(1.0F, 2.0F, 3.0F);

  CHECK(value == Vec3(1.0F, 2.0F, 3.0F));
  CHECK(value != Vec3(1.0F, 2.0F, 4.0F));
}

TEST_CASE("vec3: 加减与取负", "[core][vec3]") {
  const Vec3 a(1.0F, 2.0F, 3.0F);
  const Vec3 b(4.0F, 5.0F, 6.0F);

  CHECK(a + b == Vec3(5.0F, 7.0F, 9.0F));
  CHECK(b - a == Vec3(3.0F, 3.0F, 3.0F));
  CHECK(-a == Vec3(-1.0F, -2.0F, -3.0F));
}

TEST_CASE("vec3: 缩放与除法", "[core][vec3]") {
  const Vec3 a(1.0F, 2.0F, 3.0F);

  CHECK(a * 2.0F == Vec3(2.0F, 4.0F, 6.0F));
  CHECK(2.0F * a == Vec3(2.0F, 4.0F, 6.0F));
  CHECK(a / 2.0F == Vec3(0.5F, 1.0F, 1.5F));
}

TEST_CASE("vec3: 复合赋值", "[core][vec3]") {
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

TEST_CASE("vec3: dot 与 cross", "[core][vec3]") {
  CHECK(dot(Vec3(1.0F, 2.0F, 3.0F), Vec3(4.0F, 5.0F, 6.0F)) == 32.0F);
  CHECK(dot(Vec3(1.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F)) == 0.0F);

  CHECK(cross(Vec3(1.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F)) == Vec3(0.0F, 0.0F, 1.0F));
  CHECK(cross(Vec3(0.0F, 1.0F, 0.0F), Vec3(1.0F, 0.0F, 0.0F)) == Vec3(0.0F, 0.0F, -1.0F));
}

TEST_CASE("vec3: 长度与归一化", "[core][vec3]") {
  CHECK(Vec3(3.0F, 4.0F, 0.0F).lengthSquared() == 25.0F);
  CHECK(Vec3(3.0F, 4.0F, 0.0F).length() == Catch::Approx(5.0F));

  const Vec3 unit = Vec3(3.0F, 4.0F, 0.0F).normalized();
  CHECK(unit.x == Catch::Approx(0.6F));
  CHECK(unit.y == Catch::Approx(0.8F));
  CHECK(unit.z == Catch::Approx(0.0F));
  CHECK(unit.length() == Catch::Approx(1.0F));
}

TEST_CASE("Quat identity leaves vectors unchanged", "[math][quat]") {
  const Quat identity = Quat::identity();
  CHECK(identity == Quat{});
  checkVecApprox(identity.rotate(Vec3{1.0F, 2.0F, 3.0F}), Vec3{1.0F, 2.0F, 3.0F});
}

TEST_CASE("Quat axis-angle rotates around X and Y", "[math][quat]") {
  const Quat aroundX = Quat::fromAxisAngle(Vec3{1.0F, 0.0F, 0.0F}, std::numbers::pi_v<float> * 0.5F);
  const Quat aroundY = Quat::fromAxisAngle(Vec3{0.0F, 1.0F, 0.0F}, std::numbers::pi_v<float> * 0.5F);

  checkVecApprox(aroundX.rotate(Vec3{0.0F, 1.0F, 0.0F}), Vec3{0.0F, 0.0F, 1.0F});
  checkVecApprox(aroundY.rotate(Vec3{0.0F, 0.0F, 1.0F}), Vec3{1.0F, 0.0F, 0.0F});
}

TEST_CASE("Quat multiplication applies the right operand first", "[math][quat]") {
  const Quat aroundX = Quat::fromAxisAngle(Vec3{1.0F, 0.0F, 0.0F}, std::numbers::pi_v<float> * 0.5F);
  const Quat aroundY = Quat::fromAxisAngle(Vec3{0.0F, 1.0F, 0.0F}, std::numbers::pi_v<float> * 0.5F);
  const Vec3 value{0.0F, 1.0F, 0.0F};

  checkVecApprox((aroundY * aroundX).rotate(value), aroundY.rotate(aroundX.rotate(value)));
  checkVecApprox((aroundY * aroundX).rotate(value), Vec3{1.0F, 0.0F, 0.0F});
}

TEST_CASE("Quat normalization and rotation matrix agree", "[math][quat]") {
  const Quat source{0.0F, 0.0F, 2.0F, 2.0F};
  const Quat normalized = source.normalized();
  CHECK(normalized.x == Catch::Approx(0.0F));
  CHECK(normalized.y == Catch::Approx(0.0F));
  CHECK(normalized.z == Catch::Approx(std::sqrt(0.5F)));
  CHECK(normalized.w == Catch::Approx(std::sqrt(0.5F)));

  const Quat rotation = Quat::fromAxisAngle(Vec3{0.0F, 1.0F, 0.0F}, std::numbers::pi_v<float> * 0.5F);
  const Mat4 matrix = rotation.toMat4();
  const Vec3 vector{0.2F, -0.4F, 1.3F};
  checkVecApprox(matrix.transformVector(vector), rotation.rotate(vector));
  const Quat roundTrip = Quat::fromRotationMatrix(matrix);
  checkVecApprox(roundTrip.rotate(vector), rotation.rotate(vector));

  for (const Vec3 axis : std::array{Vec3{1.0F, 0.0F, 0.0F}, Vec3{0.0F, 1.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}}) {
    const Quat halfTurn = Quat::fromAxisAngle(axis, std::numbers::pi_v<float>);
    const Quat reconstructed = Quat::fromRotationMatrix(halfTurn.toMat4());
    checkVecApprox(reconstructed.rotate(vector), halfTurn.rotate(vector));
  }
}

TEST_CASE("Transform identity", "[math][transform]") {
  CHECK(yr::math::toMat4(Transform{}).transformPoint(Vec3{}) == Vec3{});
  CHECK(yr::math::toMat4(Transform{}).transformVector(Vec3{1.0F, 2.0F, 3.0F}) == Vec3{1.0F, 2.0F, 3.0F});
}

TEST_CASE("Transform composes translation, scale and rotation", "[math][transform]") {
  SECTION("pure translation") {
    Transform transform;
    transform.position = Vec3{3.0F, -2.0F, 5.0F};
    checkVecApprox(yr::math::toMat4(transform).transformPoint(Vec3{}), transform.position);
  }
  SECTION("pure scale") {
    Transform transform;
    transform.scale = Vec3{2.0F, 3.0F, 4.0F};
    checkVecApprox(yr::math::toMat4(transform).transformVector(Vec3{1.0F, 1.0F, 1.0F}), transform.scale);
  }
  SECTION("pure rotation") {
    Transform transform;
    transform.rotation = Quat::fromAxisAngle(Vec3{1.0F, 0.0F, 0.0F}, std::numbers::pi_v<float> * 0.5F);
    checkVecApprox(yr::math::toMat4(transform).transformVector(Vec3{0.0F, 1.0F, 0.0F}), Vec3{0.0F, 0.0F, 1.0F});
  }
  SECTION("TRS order") {
    Transform transform;
    transform.position = Vec3{1.0F, 2.0F, 3.0F};
    transform.rotation = Quat::fromAxisAngle(Vec3{1.0F, 0.0F, 0.0F}, std::numbers::pi_v<float> * 0.5F);
    transform.scale = Vec3{2.0F, 2.0F, 2.0F};
    const auto matrix = yr::math::toMat4(transform);
    checkVecApprox(matrix.transformPoint(Vec3{}), Vec3{1.0F, 2.0F, 3.0F});
    checkVecApprox(matrix.transformPoint(Vec3{1.0F, 0.0F, 0.0F}), Vec3{3.0F, 2.0F, 3.0F});
  }
}
