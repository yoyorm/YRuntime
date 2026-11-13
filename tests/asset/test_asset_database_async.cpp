#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <yr/asset/asset_database.h>
#include <yr/asset/memory_file_access.h>
#include <yr/asset/resource.h>
#include <yr/asset/resource_types.h>

namespace {

  using yr::asset::AssetDatabase;
  using yr::asset::AssetUID;
  using yr::asset::LoadState;
  using yr::asset::MemoryFileAccess;
  using yr::asset::Texture;

  // 1x1 RGBA PNG：filter 0 + 不透明红色像素。
  constexpr std::array<std::uint8_t, 70> kPng1x1 = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
      0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF,
      0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

  std::vector<std::uint8_t> pngBytes() {
    return {kPng1x1.begin(), kPng1x1.end()};
  }

  std::unique_ptr<AssetDatabase> makeDatabase() {
    auto access = std::make_unique<MemoryFileAccess>();
    access->addFile("res://pixel.png", pngBytes());
    return std::make_unique<AssetDatabase>(std::move(access));
  }

  bool pumpUntil(AssetDatabase& database, AssetUID uid, LoadState expected,
                 std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      database.pump(1);
      if (database.state(uid) == expected) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    database.pump(1);
    return database.state(uid) == expected;
  }

  yr::obj::Ref<Texture> makeReadyTexture(AssetUID uid, std::string sourcePath) {
    yr::obj::Ref<Texture> texture = yr::obj::make_ref<Texture>();
    texture->setUid(uid);
    texture->setSourcePath(std::move(sourcePath));
    REQUIRE(texture->markLoading());
    REQUIRE(texture->markReady());
    return texture;
  }

} // namespace

TEST_CASE("asset db async: requestLoad installs a texture on the main thread", "[asset][asset_db][async]") {
  const std::unique_ptr<AssetDatabase> database = makeDatabase();

  const AssetUID uid = database->requestLoad("res://pixel.png");
  REQUIRE(uid.valid());
  CHECK(uid == AssetUID::fromPath("res://pixel.png"));
  CHECK(database->state(uid) == LoadState::kLoading);

  REQUIRE(pumpUntil(*database, uid, LoadState::kReady));
  CHECK(database->error(uid).empty());

  yr::obj::Ref<Texture> texture = database->findAs<Texture>(uid);
  REQUIRE(texture.valid());
  CHECK(texture->width == 1);
  CHECK(texture->height == 1);
  REQUIRE(texture->rgba8.size() == 4);
  CHECK(texture->state() == LoadState::kReady);

  const yr::asset::AssetStats stats = database->stats();
  CHECK(stats.resident_count == 1);
  CHECK(stats.loads == 1);
  CHECK(stats.failed_loads == 0);

  texture.reset();
  REQUIRE(database->evict(uid)); // requestLoad 不交出 Ref，DB 独占
  CHECK(database->state(uid) == LoadState::kUnloaded);
  CHECK(database->stats().resident_count == 0);
}

TEST_CASE("asset db async: requestLoad carries color space and dimension cap", "[asset][asset_db][async]") {
  const std::unique_ptr<AssetDatabase> database = makeDatabase();

  const AssetUID uid = database->requestLoad("res://pixel.png", yr::asset::ResourceKind::kTexture,
                                             yr::asset::TextureColorSpace::kLinear, 1);
  REQUIRE(uid.valid());
  REQUIRE(pumpUntil(*database, uid, LoadState::kReady));

  const yr::obj::Ref<Texture> texture = database->findAs<Texture>(uid);
  REQUIRE(texture.valid());
  CHECK(texture->colorSpace == yr::asset::TextureColorSpace::kLinear);
  CHECK(texture->width == 1);
  CHECK(texture->height == 1);
  REQUIRE(texture->rgba8.size() == 4);
}

TEST_CASE("asset db async: duplicate request merges into one load", "[asset][asset_db][async]") {
  const std::unique_ptr<AssetDatabase> database = makeDatabase();

  const AssetUID first = database->requestLoad("res://pixel.png");
  const AssetUID second = database->requestLoad("res://pixel.png");
  REQUIRE(first.valid());
  CHECK(second == first);
  CHECK(database->stats().loads == 1);

  REQUIRE(pumpUntil(*database, first, LoadState::kReady));
  CHECK(database->stats().loads == 1);

  const AssetUID third = database->requestLoad("res://pixel.png");
  CHECK(third == first);
  CHECK(database->state(first) == LoadState::kReady);
  CHECK(database->stats().loads == 1);
}

TEST_CASE("asset db async: invalid png fails without caching a texture", "[asset][asset_db][async]") {
  auto access = std::make_unique<MemoryFileAccess>();
  access->addFile("res://broken.png", std::vector<std::uint8_t>{0x00, 0x01, 0x02, 0x03});
  AssetDatabase database(std::move(access));

  const AssetUID uid = database.requestLoad("res://broken.png");
  REQUIRE(uid.valid());
  CHECK(database.state(uid) == LoadState::kLoading);

  REQUIRE(pumpUntil(database, uid, LoadState::kFailed));
  CHECK_FALSE(database.error(uid).empty());
  CHECK_FALSE(database.contains(uid));
  CHECK_FALSE(database.findAs<Texture>(uid).valid());

  const yr::asset::AssetStats stats = database.stats();
  CHECK(stats.resident_count == 0);
  CHECK(stats.failed_loads == 1);
}

TEST_CASE("asset db async: evict forgets state so the next request reloads", "[asset][asset_db][async]") {
  const std::unique_ptr<AssetDatabase> database = makeDatabase();

  const AssetUID uid = database->requestLoad("res://pixel.png");
  REQUIRE(pumpUntil(*database, uid, LoadState::kReady));
  REQUIRE(database->evict(uid));
  CHECK(database->state(uid) == LoadState::kUnloaded);
  CHECK(database->error(uid).empty());

  const AssetUID again = database->requestLoad("res://pixel.png");
  CHECK(again == uid);
  CHECK(database->state(uid) == LoadState::kLoading);
  CHECK(database->stats().loads == 2);

  REQUIRE(pumpUntil(*database, uid, LoadState::kReady));
  CHECK(database->findAs<Texture>(uid).valid());
}

TEST_CASE("asset db async: collectGarbage forgets state so the next request reloads", "[asset][asset_db][async][gc]") {
  const std::unique_ptr<AssetDatabase> database = makeDatabase();

  const AssetUID uid = database->requestLoad("res://pixel.png");
  REQUIRE(pumpUntil(*database, uid, LoadState::kReady));

  const yr::asset::GcStats gc = database->collectGarbage();
  CHECK(gc.evicted == 1);
  CHECK(database->stats().resident_count == 0);
  CHECK(database->state(uid) == LoadState::kUnloaded);

  const AssetUID again = database->requestLoad("res://pixel.png");
  CHECK(again == uid);
  CHECK(database->state(uid) == LoadState::kLoading);
  CHECK(database->stats().loads == 2);
}

TEST_CASE("asset db async: clear drops pending loader state", "[asset][asset_db][async]") {
  const std::unique_ptr<AssetDatabase> database = makeDatabase();

  const AssetUID uid = database->requestLoad("res://pixel.png");
  REQUIRE(uid.valid());
  database->clear();

  CHECK_FALSE(database->contains(uid));
  CHECK(database->state(uid) == LoadState::kUnloaded);
  CHECK(database->error(uid).empty());
}

TEST_CASE("asset db async: non-texture requests fail at install time", "[asset][asset_db][async]") {
  const std::unique_ptr<AssetDatabase> database = makeDatabase();

  const AssetUID uid = database->requestLoad("res://pixel.png", yr::asset::ResourceKind::kMesh);
  REQUIRE(uid.valid());
  REQUIRE(pumpUntil(*database, uid, LoadState::kFailed));
  CHECK_FALSE(database->error(uid).empty());
  CHECK_FALSE(database->contains(uid));
  CHECK(database->stats().failed_loads == 1);
}

TEST_CASE("asset db async: cached ready resource skips redundant request", "[asset][asset_db][async]") {
  const std::unique_ptr<AssetDatabase> database = makeDatabase();
  const AssetUID uid = AssetUID::fromPath("res://pixel.png");

  const yr::obj::Ref<Texture> texture = makeReadyTexture(uid, "res://pixel.png");
  REQUIRE(database->store(texture));

  const std::uint64_t loadsBefore = database->stats().loads;
  CHECK(database->requestLoad("res://pixel.png", yr::asset::ResourceKind::kTexture) == uid);
  CHECK(database->stats().loads == loadsBefore);
  CHECK(database->state(uid) == LoadState::kReady);
  CHECK(database->error(uid).empty());
}

TEST_CASE("asset db async: cached ready resource rejects kind mismatch", "[asset][asset_db][async]") {
  const std::unique_ptr<AssetDatabase> database = makeDatabase();
  const AssetUID uid = AssetUID::fromPath("res://pixel.png");

  const yr::obj::Ref<Texture> texture = makeReadyTexture(uid, "res://pixel.png");
  REQUIRE(database->store(texture));

  const std::uint64_t loadsBefore = database->stats().loads;
  CHECK_FALSE(database->requestLoad("res://pixel.png", yr::asset::ResourceKind::kMesh).valid());
  CHECK(database->stats().loads == loadsBefore);
  CHECK(database->state(uid) == LoadState::kReady);
}

TEST_CASE("asset db async: failed completion marks cached placeholder failed", "[asset][asset_db][async]") {
  auto access = std::make_unique<MemoryFileAccess>();
  access->addFile("res://bad.png", std::vector<std::uint8_t>{0x00, 0x01, 0x02, 0x03});
  AssetDatabase database(std::move(access));

  const AssetUID uid = AssetUID::fromPath("res://bad.png");
  yr::obj::Ref<Texture> placeholder = yr::obj::make_ref<Texture>();
  placeholder->setUid(uid);
  placeholder->setSourcePath("res://bad.png");
  REQUIRE(placeholder->markLoading());
  REQUIRE(database.store(placeholder));

  REQUIRE(database.requestLoad("res://bad.png", yr::asset::ResourceKind::kTexture) == uid);
  REQUIRE(pumpUntil(database, uid, LoadState::kFailed));
  CHECK_FALSE(database.error(uid).empty());

  yr::obj::Ref<Texture> cached = database.findAs<Texture>(uid);
  REQUIRE(cached.valid());
  CHECK(cached->state() == LoadState::kFailed);
  CHECK_FALSE(cached->error().empty());
  cached.reset();
  placeholder.reset();
  REQUIRE(database.evict(uid));
  CHECK(database.state(uid) == LoadState::kUnloaded);
}

TEST_CASE("asset db async: cached ready resource hides stale loader error", "[asset][asset_db][async]") {
  auto access = std::make_unique<MemoryFileAccess>();
  access->addFile("res://broken.png", std::vector<std::uint8_t>{0x00, 0x01, 0x02, 0x03});
  AssetDatabase database(std::move(access));

  const AssetUID uid = AssetUID::fromPath("res://broken.png");
  REQUIRE(database.requestLoad("res://broken.png", yr::asset::ResourceKind::kTexture) == uid);
  REQUIRE(pumpUntil(database, uid, LoadState::kFailed));
  REQUIRE_FALSE(database.error(uid).empty());
  REQUIRE_FALSE(database.contains(uid));

  const yr::obj::Ref<Texture> texture = makeReadyTexture(uid, "res://broken.png");
  REQUIRE(database.store(texture));

  const std::uint64_t loadsBefore = database.stats().loads;
  CHECK(database.state(uid) == LoadState::kReady);
  CHECK(database.error(uid).empty());
  CHECK(database.requestLoad("res://broken.png", yr::asset::ResourceKind::kTexture) == uid);
  CHECK(database.stats().loads == loadsBefore);
  CHECK(database.state(uid) == LoadState::kReady);
}
