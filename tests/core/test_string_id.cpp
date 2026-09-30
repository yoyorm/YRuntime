#include <catch2/catch_test_macros.hpp>

#include <yr/core/string_interner.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using yr::core::StringId;
using yr::core::StringInterner;

static_assert(sizeof(StringId) == sizeof(const void*));
static_assert(std::is_trivially_copyable_v<StringId>);
static_assert(!std::is_convertible_v<std::uint32_t, StringId>);
static_assert(!std::is_default_constructible_v<StringInterner>);

TEST_CASE("string id: default value is invalid", "[core][string_id]") {
  const StringId id{};

  CHECK_FALSE(id.valid());
  CHECK(id == StringId::invalid());
  CHECK(id.view().empty());
}

TEST_CASE("string interner: equal text returns the same id", "[core][string_id][interner]") {
  StringInterner& interner = yr::core::globalStringInterner();
  const std::size_t initialSize = interner.size();

  const StringId first = interner.intern("string_id_equal_player");
  const StringId second = interner.intern(std::string("string_id_equal_player"));

  REQUIRE(first.valid());
  CHECK(first == second);
  CHECK(interner.size() == initialSize + 1);
  CHECK(first.view() == "string_id_equal_player");
  CHECK(interner.lookup(second) == "string_id_equal_player");
}

TEST_CASE("string interner: different text gets different ids", "[core][string_id][interner]") {
  StringInterner& interner = yr::core::globalStringInterner();
  const std::size_t initialSize = interner.size();

  const StringId player = interner.intern("string_id_different_player");
  const StringId enemy = interner.intern("string_id_different_enemy");

  REQUIRE(player.valid());
  REQUIRE(enemy.valid());
  CHECK(player != enemy);
  CHECK(interner.size() == initialSize + 2);
  CHECK(player.view() == "string_id_different_player");
  CHECK(enemy.view() == "string_id_different_enemy");
}

TEST_CASE("string interner: empty text is a valid interned string", "[core][string_id][interner]") {
  StringInterner& interner = yr::core::globalStringInterner();
  const std::size_t initialSize = interner.size();

  const StringId id = interner.intern("");

  CHECK(id.valid());
  CHECK(id != StringId::invalid());
  CHECK(id.view().empty());
  CHECK(interner.lookup(id).empty());
  CHECK(interner.size() == initialSize + 1);
}

TEST_CASE("string interner: interned text owns a copy of the input", "[core][string_id][interner]") {
  StringInterner& interner = yr::core::globalStringInterner();
  std::string source = "temporary";

  const StringId id = interner.intern(source);
  source.assign("changed");

  REQUIRE(id.valid());
  CHECK(interner.lookup(id) == "temporary");
  CHECK(id.view() == "temporary");
}

TEST_CASE("string interner: ids remain valid after storage grows", "[core][string_id][interner]") {
  StringInterner& interner = yr::core::globalStringInterner();
  const std::size_t initialSize = interner.size();
  const StringId first = interner.intern("first");
  std::vector<StringId> ids;
  ids.reserve(10'000);

  for (std::size_t index = 0; index < 10'000; ++index) {
    ids.push_back(interner.intern("name_" + std::to_string(index)));
  }

  REQUIRE(first.valid());
  CHECK(first.view() == "first");
  CHECK(interner.lookup(first) == "first");
  CHECK(interner.size() == initialSize + ids.size() + 1);

  for (std::size_t index = 0; index < ids.size(); ++index) {
    CHECK(ids[index].view() == "name_" + std::to_string(index));
  }
}

TEST_CASE("string id: can be used as an unordered key", "[core][string_id]") {
  StringInterner& interner = yr::core::globalStringInterner();
  const StringId player = interner.intern("string_id_hash_player");
  const StringId enemy = interner.intern("string_id_hash_enemy");

  std::unordered_set<StringId> ids;
  ids.insert(player);
  ids.insert(interner.intern("string_id_hash_player"));
  ids.insert(enemy);

  std::unordered_map<StringId, int> values;
  values[player] = 1;
  values[enemy] = 2;

  CHECK(ids.size() == 2);
  CHECK(values.at(interner.intern("string_id_hash_player")) == 1);
  CHECK(values.at(interner.intern("string_id_hash_enemy")) == 2);
}

TEST_CASE("string interner: global access uses one id space", "[core][string_id][interner]") {
  StringInterner& first = yr::core::globalStringInterner();
  StringInterner& second = yr::core::globalStringInterner();

  CHECK(&first == &second);
  CHECK(first.intern("global_name") == second.intern("global_name"));
}
