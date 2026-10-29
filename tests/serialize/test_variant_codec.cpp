#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include <yr/object/variant.h>
#include <yr/serialize/variant_codec.h>

namespace {

  using yr::math::Vec3;
  using yr::obj::Array;
  using yr::obj::Variant;
  using yr::ser::VariantCodec;
  using yr::ser::VariantDecodeResult;
  using yr::ser::VariantEncodeResult;

  [[nodiscard]] Variant roundTrip(const Variant& value) {
    const VariantEncodeResult encoded = VariantCodec::encode(value);
    REQUIRE(encoded.ok);
    CHECK_FALSE(encoded.text.empty());

    const VariantDecodeResult decoded = VariantCodec::decode(encoded.text);
    CAPTURE(encoded.text);
    REQUIRE(decoded.ok);
    CHECK(decoded.consumed == encoded.text.size());
    return decoded.value;
  }

} // namespace

TEST_CASE("variant codec: bool / int / double round-trip", "[serialize][variant]") {
  CHECK(roundTrip(Variant(true)).as<bool>());
  CHECK_FALSE(roundTrip(Variant(false)).as<bool>());

  CHECK(roundTrip(Variant(std::int64_t{0})).as<std::int64_t>() == 0);
  CHECK(roundTrip(Variant(std::int64_t{-7})).as<std::int64_t>() == -7);
  CHECK(roundTrip(Variant(std::numeric_limits<std::int64_t>::min())).as<std::int64_t>() ==
        std::numeric_limits<std::int64_t>::min());

  for (const double value : {0.1, -0.25, 1.0 / 3.0, 1e-300, 1e300}) {
    CAPTURE(value);
    CHECK(roundTrip(Variant(value)).as<double>() == value);
  }
  CHECK(roundTrip(Variant(-0.0)).as<double>() == 0.0);
}

TEST_CASE("variant codec: double uses max_digits10 and stays exact", "[serialize][variant]") {
  const double value = 0.1 + 0.2;
  const VariantEncodeResult encoded = VariantCodec::encode(Variant(value));
  REQUIRE(encoded.ok);
  const VariantDecodeResult decoded = VariantCodec::decode(encoded.text);
  REQUIRE(decoded.ok);
  CHECK(decoded.value.as<double>() == value);

  const double third = 1.0 / 3.0;
  const VariantEncodeResult thirdEncoded = VariantCodec::encode(Variant(third));
  REQUIRE(thirdEncoded.ok);
  CHECK(VariantCodec::decode(thirdEncoded.text).value.as<double>() == third);
}

TEST_CASE("variant codec: Vector3 components round-trip as float", "[serialize][variant]") {
  const Vec3 value{3.1415927F, 1.0F / 3.0F, -0.000123456789F};
  const Variant decoded = roundTrip(Variant(value));
  REQUIRE(decoded.type() == Variant::Type::kVec3);
  CHECK(decoded.as<Vec3>() == value);
}

TEST_CASE("variant codec: string escapes and UTF-8 round-trip", "[serialize][variant]") {
  const std::string value = "a\"b\\c\nd\te\rf \xE5\x9C\xBA\xE6\x99\xAF";
  const VariantEncodeResult encoded = VariantCodec::encode(Variant(value));
  REQUIRE(encoded.ok);
  CHECK(encoded.text.front() == '"');
  CHECK(encoded.text.back() == '"');
  CHECK(encoded.text.find("\\\"") != std::string::npos);
  CHECK(encoded.text.find("\\\\") != std::string::npos);
  CHECK(encoded.text.find("\\n") != std::string::npos);
  CHECK(encoded.text.find("\\t") != std::string::npos);
  CHECK(encoded.text.find("\\r") != std::string::npos);

  const VariantDecodeResult decoded = VariantCodec::decode(encoded.text);
  REQUIRE(decoded.ok);
  CHECK(decoded.value.as<std::string>() == value);
}

TEST_CASE("variant codec: invalid syntax returns an error", "[serialize][variant]") {
  for (const char* text :
       {"tru", "\"unterminated", "Vector3(1, 2)", "Vector3(1, 2, 3,)", "nan", "1e999", "99999999999999999999", ""}) {
    CAPTURE(text);
    const VariantDecodeResult decoded = VariantCodec::decode(text);
    CHECK_FALSE(decoded.ok);
    CHECK_FALSE(decoded.error.empty());
  }
}

TEST_CASE("variant codec: unsupported Variant types cannot be encoded", "[serialize][variant]") {
  const VariantEncodeResult encoded = VariantCodec::encode(Variant(Array{}));
  CHECK_FALSE(encoded.ok);
  CHECK_FALSE(encoded.error.empty());

  const VariantEncodeResult nullValue = VariantCodec::encode(Variant{});
  CHECK_FALSE(nullValue.ok);
}

TEST_CASE("variant codec: decode can start at an offset", "[serialize][variant]") {
  const VariantDecodeResult decoded = VariantCodec::decode("  true rest", 0);
  REQUIRE(decoded.ok);
  CHECK(decoded.value.as<bool>());
  CHECK(decoded.consumed == 6);
}