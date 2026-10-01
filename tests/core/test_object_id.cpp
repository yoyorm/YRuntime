#include <catch2/catch_test_macros.hpp>

#include <yr/core/handle.h>
#include <yr/core/object_id.h>
#include <yr/core/string_id.h>

#include <cstdint>
#include <type_traits>

namespace {

  using IntHandle = yr::core::Handle<int>;
  using ObjectID = yr::core::ObjectID;
  using StringId = yr::core::StringId;

} // namespace

static_assert(sizeof(ObjectID) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<ObjectID>);
static_assert(std::is_default_constructible_v<ObjectID>);
static_assert(!std::is_constructible_v<ObjectID, std::uint64_t>);
static_assert(!std::is_convertible_v<std::uint64_t, ObjectID>);
static_assert(!std::is_convertible_v<ObjectID, IntHandle>);
static_assert(!std::is_convertible_v<IntHandle, ObjectID>);
static_assert(!std::is_convertible_v<ObjectID, StringId>);
static_assert(!std::is_convertible_v<StringId, ObjectID>);
static_assert(yr::core::ObjectIDBits::kIndexBits + yr::core::ObjectIDBits::kGenerationBits == 64);

TEST_CASE("object id: default value is invalid", "[core][object_id]") {
  const ObjectID id{};

  CHECK_FALSE(id.valid());
  CHECK(id == ObjectID::invalid());
  CHECK(id.raw() == 0);
  CHECK(id.index() == 0);
  CHECK(id.generation() == 0);
}
