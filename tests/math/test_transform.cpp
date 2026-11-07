#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include <yr/math/transform.h>

using yr::math::Quat;
using yr::math::Transform;
using yr::math::Vec3;

namespace {

  void checkVecApprox(Vec3 actual, Vec3 expected) {
    CHECK(actual.x == Catch::Approx(expected.x).margin(1.0e-5F));
    CHECK(actual.y == Catch::Approx(expected.y).margin(1.0e-5F));
    CHECK(actual.z == Catch::Approx(expected.z).margin(1.0e-5F));
  }

} // namespace

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
