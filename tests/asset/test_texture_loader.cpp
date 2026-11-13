#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <yr/asset/texture_loader.h>

namespace {

  using yr::asset::DecodedTexture;

  // 1x1 RGBA PNG：filter 0 + 不透明红色像素。
  constexpr std::array<std::uint8_t, 70> kPng1x1 = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
      0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF,
      0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

} // namespace

TEST_CASE("texture loader: downscaleRgba8 keeps pixels when under limit", "[asset][texture]") {
  const std::array<std::uint8_t, 16> pixels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  for (const std::uint32_t maxDimension : {0U, 2U, 4U}) {
    DecodedTexture decoded;
    yr::asset::downscaleRgba8(pixels.data(), 2, 2, maxDimension, decoded);
    CHECK(decoded.width == 2);
    CHECK(decoded.height == 2);
    CHECK(decoded.rgba8 == std::vector<std::uint8_t>(pixels.begin(), pixels.end()));
  }
}

TEST_CASE("texture loader: downscaleRgba8 averages 2x2 into 1x1", "[asset][texture]") {
  const std::array<std::uint8_t, 16> pixels{
      255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
  };

  DecodedTexture decoded;
  yr::asset::downscaleRgba8(pixels.data(), 2, 2, 1, decoded);
  REQUIRE(decoded.width == 1);
  REQUIRE(decoded.height == 1);
  REQUIRE(decoded.rgba8.size() == 4);
  CHECK(decoded.rgba8[0] == 128);
  CHECK(decoded.rgba8[1] == 128);
  CHECK(decoded.rgba8[2] == 128);
  CHECK(decoded.rgba8[3] == 255);
}

TEST_CASE("texture loader: downscaleRgba8 preserves aspect ratio", "[asset][texture]") {
  const std::vector<std::uint8_t> pixels(4 * 2 * 4, 64);

  DecodedTexture decoded;
  yr::asset::downscaleRgba8(pixels.data(), 4, 2, 2, decoded);
  REQUIRE(decoded.width == 2);
  REQUIRE(decoded.height == 1);
  REQUIRE(decoded.rgba8.size() == 2 * 1 * 4);
  CHECK(decoded.rgba8[0] == 64);
  CHECK(decoded.rgba8[3] == 64);
}

TEST_CASE("texture loader: downscaleRgba8 rejects empty input", "[asset][texture]") {
  const std::array<std::uint8_t, 16> pixels{};
  {
    DecodedTexture decoded;
    decoded.width = 9;
    decoded.height = 9;
    decoded.rgba8.assign(4, 0);
    yr::asset::downscaleRgba8(nullptr, 2, 2, 1, decoded);
    CHECK(decoded.width == 0);
    CHECK(decoded.height == 0);
    CHECK(decoded.rgba8.empty());
  }
  {
    DecodedTexture decoded;
    decoded.width = 9;
    decoded.height = 9;
    decoded.rgba8.assign(4, 0);
    yr::asset::downscaleRgba8(pixels.data(), 0, 2, 1, decoded);
    CHECK(decoded.width == 0);
    CHECK(decoded.height == 0);
    CHECK(decoded.rgba8.empty());
  }
}

TEST_CASE("texture loader: decodeRgba8 decodes a PNG", "[asset][texture]") {
  DecodedTexture decoded;
  std::string error;
  REQUIRE(yr::asset::decodeRgba8(kPng1x1.data(), kPng1x1.size(), 1, decoded, error));
  CHECK(error.empty());
  CHECK(decoded.width == 1);
  CHECK(decoded.height == 1);
  CHECK(decoded.rgba8.size() == 4);
}

TEST_CASE("texture loader: decodeRgba8 rejects invalid bytes", "[asset][texture]") {
  const std::array<std::uint8_t, 4> bytes{0x00, 0x01, 0x02, 0x03};
  DecodedTexture decoded;
  std::string error;
  CHECK_FALSE(yr::asset::decodeRgba8(bytes.data(), bytes.size(), 1, decoded, error));
  CHECK_FALSE(error.empty());
  CHECK(decoded.width == 0);
  CHECK(decoded.height == 0);
  CHECK(decoded.rgba8.empty());
}
