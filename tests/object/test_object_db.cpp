#include <catch2/catch_test_macros.hpp>

#include <yr/object/object.h>
#include <yr/object/object_db.h>

#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace {

  class TestObject final : public yr::obj::Object {};

} // namespace

static_assert(!std::is_default_constructible_v<yr::obj::ObjectDB>);
static_assert(!std::is_copy_constructible_v<yr::obj::Object>);
static_assert(!std::is_copy_assignable_v<yr::obj::Object>);
static_assert(!std::is_move_constructible_v<yr::obj::Object>);
static_assert(!std::is_move_assignable_v<yr::obj::Object>);

TEST_CASE("object: construction registers identity in ObjectDB", "[object][object_db][lifecycle]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t initialCount = database.aliveCount();
  TestObject object;

  REQUIRE(object.id().valid());
  CHECK(database.contains(object.id()));
  CHECK(database.get(object.id()) == &object);
  CHECK(database.aliveCount() == initialCount + 1);
}

TEST_CASE("object: destruction unregisters identity from ObjectDB", "[object][object_db][lifecycle]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  const std::size_t initialCount = database.aliveCount();
  yr::core::ObjectID expired;

  {
    TestObject object;
    expired = object.id();
    REQUIRE(database.contains(expired));
    CHECK(database.aliveCount() == initialCount + 1);
  }

  CHECK_FALSE(database.contains(expired));
  CHECK(database.get(expired) == nullptr);
  CHECK(database.aliveCount() == initialCount);
}

TEST_CASE("object db: reused slot gets a new generation", "[object][object_db][lifecycle]") {
  yr::obj::ObjectDB& database = yr::obj::globalObjectDB();
  yr::core::ObjectID first;

  {
    TestObject object;
    first = object.id();
  }

  TestObject secondObject;
  const yr::core::ObjectID second = secondObject.id();

  CHECK(first.index() == second.index());
  CHECK(first.generation() != second.generation());
  CHECK(first != second);
  CHECK(database.get(first) == nullptr);
  CHECK(database.get(second) == &secondObject);
}

TEST_CASE("object db: distinct live objects have distinct ids", "[object][object_db]") {
  TestObject first;
  TestObject second;

  CHECK(first.id() != second.id());
  CHECK(yr::obj::globalObjectDB().get(first.id()) == &first);
  CHECK(yr::obj::globalObjectDB().get(second.id()) == &second);
}

TEST_CASE("object id: valid ids can be used as unordered keys", "[object][object_db]") {
  TestObject first;
  TestObject second;

  std::unordered_set<yr::core::ObjectID> ids;
  ids.insert(first.id());
  ids.insert(second.id());
  ids.insert(first.id());

  std::unordered_map<yr::core::ObjectID, int> values;
  values[first.id()] = 10;
  values[second.id()] = 20;

  CHECK(ids.size() == 2);
  CHECK(values.at(first.id()) == 10);
  CHECK(values.at(second.id()) == 20);
}
