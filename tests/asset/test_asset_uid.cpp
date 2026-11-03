#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <unordered_set>

#include <yr/asset/asset_uid.h>

namespace {

  [[nodiscard]] bool isLowerHex(std::string_view text) {
    if (text.empty()) {
      return false;
    }
    for (const char c : text) {
      const bool digit = c >= '0' && c <= '9';
      const bool lower = c >= 'a' && c <= 'f';
      if (!digit && !lower) {
        return false;
      }
    }
    return true;
  }

} // namespace

TEST_CASE("asset uid: canonical res:// paths pass through unchanged", "[asset][uid]") {
  const char* const paths[] = {
      "res://a",
      "res://textures/test.png",
      "res://textures/sub-dir/file_01.png",
      "res://中文.png",
  };

  for (const char* const path : paths) {
    CAPTURE(path);
    const yr::asset::AssetPath result = yr::asset::canonicalizeAssetPath(path);
    REQUIRE(result.ok());
    CHECK(result.error.empty());
    CHECK(result.canonical == path);
  }
}

TEST_CASE("asset uid: invalid canonical paths are rejected", "[asset][uid]") {
  const char* const paths[] = {
      "",
      "a.png",
      "res:/a",
      "res://",
      "res:///a",
      "res://a//b",
      "res://a/",
      "res://a/../b",
      "res://a/./b",
      "res://a\\b",
      "res://a\"b",
      "res://a\nb",
      "res://a\tb",
      "res://a/"
      "\x01"
      "b",
      "RES://a",
  };

  for (const char* const path : paths) {
    CAPTURE(path);
    const yr::asset::AssetPath result = yr::asset::canonicalizeAssetPath(path);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.error.empty());
    CHECK(result.canonical.empty());
  }
}

TEST_CASE("asset uid: fromPath is deterministic and distinct per path", "[asset][uid]") {
  const yr::asset::AssetUID first = yr::asset::AssetUID::fromPath("res://textures/test.png");
  const yr::asset::AssetUID same = yr::asset::AssetUID::fromPath("res://textures/test.png");
  const yr::asset::AssetUID other = yr::asset::AssetUID::fromPath("res://textures/test2.png");

  REQUIRE(first.valid());
  CHECK(first.value != 0);
  CHECK(first == same);
  CHECK(first != other);
}

TEST_CASE("asset uid: known FNV-1a vector", "[asset][uid]") {
  const yr::asset::AssetUID uid = yr::asset::AssetUID::fromPath("res://textures/test.png");
  REQUIRE(uid.valid());
  CHECK(uid.toString() == "fe07f4b0018293a1");
}

TEST_CASE("asset uid: invalid paths produce the invalid uid", "[asset][uid]") {
  CHECK_FALSE(yr::asset::AssetUID{}.valid());
  CHECK_FALSE(yr::asset::AssetUID::fromPath("res://").valid());
  CHECK_FALSE(yr::asset::AssetUID::fromPath("not a path").valid());
  CHECK(yr::asset::AssetUID::fromPath("res://").value == 0);
}

TEST_CASE("asset uid: toString is 16 lowercase hex characters", "[asset][uid]") {
  const yr::asset::AssetUID uid = yr::asset::AssetUID::fromPath("res://textures/test.png");
  const std::string text = uid.toString();
  CHECK(text.size() == 16);
  CHECK(isLowerHex(text));
  CHECK(yr::asset::AssetUID{}.toString() == "0000000000000000");
}

TEST_CASE("asset uid: usable as an unordered key", "[asset][uid]") {
  const yr::asset::AssetUID first = yr::asset::AssetUID::fromPath("res://a");
  const yr::asset::AssetUID second = yr::asset::AssetUID::fromPath("res://b");

  std::unordered_set<yr::asset::AssetUID> ids;
  ids.insert(first);
  ids.insert(first);
  ids.insert(second);
  CHECK(ids.size() == 2);
}