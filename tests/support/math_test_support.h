#pragma once

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <yr/math/vec3.h>

namespace yr_test {

  inline void checkVecApprox(yr::math::Vec3 actual, yr::math::Vec3 expected) {
    CHECK(actual.x == Catch::Approx(expected.x).margin(1.0e-5F));
    CHECK(actual.y == Catch::Approx(expected.y).margin(1.0e-5F));
    CHECK(actual.z == Catch::Approx(expected.z).margin(1.0e-5F));
  }

} // namespace yr_test