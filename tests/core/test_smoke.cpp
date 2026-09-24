#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <yr/core/version.h>

TEST_CASE("smoke: 构建系统把 CMake 的数据正确注入到了 C++", "[build][smoke]") {
  SECTION("版本号来自 project(VERSION)") {
    REQUIRE(std::strcmp(yr::core::versionString(), "0.1.0") == 0);
  }
  SECTION("include 路径 <yr/core/...> 可用（分层约定生效）") {
    REQUIRE(yr::core::versionString() != nullptr);
  }
  SECTION("构建配置名非空") {
    REQUIRE(std::strlen(yr::core::buildConfig()) > 0);
  }
}