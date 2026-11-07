#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <numbers>

#include <yr/math/quat.h>

using yr::math::Mat4;
using yr::math::Quat;
using yr::math::Vec3;

namespace {

  void checkVecApprox(Vec3 actual, Vec3 expected) {
    CHECK(actual.x == Catch::Approx(expected.x).margin(1.0e-5F));
    CHECK(actual.y == Catch::Approx(expected.y).margin(1.0e-5F));
    CHECK(actual.z == Catch::Approx(expected.z).margin(1.0e-5F));
  }

} // namespace

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
