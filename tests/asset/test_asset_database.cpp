#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <yr/asset/asset_database.h>
#include <yr/asset/gltf_loader.h>
#include <yr/asset/resource_types.h>
#include <yr/object/object_db.h>
#include <yr/object/ref.h>

namespace {

  using yr::asset::AssetDatabase;
  using yr::asset::AssetStats;
  using yr::asset::AssetUID;
  using yr::asset::GcStats;
  using yr::asset::GltfLoadResult;
  using yr::asset::LoadState;
  using yr::asset::Material;
  using yr::asset::Mesh;
  using yr::asset::MeshVertex;
  using yr::asset::Texture;

  template <typename T> yr::obj::Ref<T> makeLoaded(AssetUID uid, std::string sourcePath) {
    yr::obj::Ref<T> resource = yr::obj::make_ref<T>();
    resource->setUid(uid);
    resource->setSourcePath(std::move(sourcePath));
    REQUIRE(resource->markLoading());
    REQUIRE(resource->markReady());
    return resource;
  }

  bool pumpUntilAllReady(AssetDatabase& database, const std::vector<yr::asset::GltfTextureRequest>& requests,
                         std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
    const auto allReady = [&] {
      return std::all_of(requests.begin(), requests.end(), [&](const yr::asset::GltfTextureRequest& request) {
        return database.state(request.uid) == LoadState::kReady;
      });
    };
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      database.pump(1);
      if (allReady()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return allReady();
  }

} // namespace

static_assert(!std::is_copy_constructible_v<AssetDatabase>);
static_assert(!std::is_move_constructible_v<AssetDatabase>);

TEST_CASE("asset db: store/find/findAs/pathFor", "[asset][asset_db]") {
  AssetDatabase database("res://assets");

  auto mesh = makeLoaded<Mesh>(AssetUID{1}, "res://meshes/quad.mesh");
  auto material = makeLoaded<Material>(AssetUID{2}, "res://materials/default.material");
  auto texture = makeLoaded<Texture>(AssetUID{3}, "res://textures/white.png");

  REQUIRE(database.store(mesh));
  REQUIRE(database.store(material));
  REQUIRE(database.store(texture));

  CHECK(database.contains(AssetUID{1}));
  CHECK(database.contains(AssetUID{2}));
  CHECK(database.contains(AssetUID{3}));
  CHECK_FALSE(database.contains(AssetUID{4}));

  CHECK(database.find(AssetUID{1}).get() == mesh.get());
  CHECK(database.findAs<Mesh>(AssetUID{1}).get() == mesh.get());
  CHECK(database.findAs<Material>(AssetUID{2}).get() == material.get());
  CHECK(database.findAs<Texture>(AssetUID{3}).get() == texture.get());
  CHECK_FALSE(database.findAs<Texture>(AssetUID{1}).valid());

  CHECK(database.pathFor(AssetUID{1}) == "res://meshes/quad.mesh");
  CHECK(database.pathFor(AssetUID{2}) == "res://materials/default.material");
  CHECK(database.pathFor(AssetUID{3}) == "res://textures/white.png");
  CHECK(database.pathFor(AssetUID{4}).empty());

  CHECK(database.assetRoot() == "res://assets");
}

TEST_CASE("asset db: duplicate store rejects UID conflicts", "[asset][asset_db]") {
  AssetDatabase database("res://assets");

  auto first = makeLoaded<Mesh>(AssetUID{1}, "res://meshes/first.mesh");
  auto second = makeLoaded<Mesh>(AssetUID{1}, "res://meshes/second.mesh");
  auto invalid = makeLoaded<Mesh>(AssetUID{}, "res://meshes/invalid.mesh");
  yr::obj::Ref<Mesh> nullMesh;

  REQUIRE(database.store(first));
  CHECK(database.store(first)); // 同一对象重复注册是幂等的
  CHECK(database.stats().registered == 1);

  CHECK_FALSE(database.store(second));
  CHECK_FALSE(database.store(nullMesh));
  CHECK_FALSE(database.store(invalid));
  CHECK(database.stats().registered == 1);

  CHECK(database.find(AssetUID{1}).get() == first.get());
  CHECK(database.pathFor(AssetUID{1}) == "res://meshes/first.mesh");
  CHECK_FALSE(database.contains(AssetUID{}));
}

TEST_CASE("asset db: collectGarbage evicts DB-only resources", "[asset][asset_db][gc]") {
  yr::obj::ObjectDB& objectDatabase = yr::obj::globalObjectDB();
  const std::size_t baseline = objectDatabase.aliveCount();

  AssetDatabase database("res://assets");
  const AssetUID uid{1};
  yr::core::ObjectID objectId;

  {
    auto mesh = makeLoaded<Mesh>(uid, "res://meshes/quad.mesh");
    objectId = mesh->id();
    REQUIRE(database.store(mesh));
    CHECK(mesh->refcount() == 2);
  }

  REQUIRE(objectDatabase.contains(objectId));
  CHECK(database.find(uid)->refcount() == 2); // 临时 find 引用 + DB 引用

  const yr::asset::GcStats gc = database.collectGarbage();
  CHECK(gc.evicted == 1);
  CHECK(gc.passes == 1);

  const yr::asset::AssetStats stats = database.stats();
  CHECK(stats.resident_count == 0);
  CHECK(stats.cpu_bytes == 0);
  CHECK(stats.evicted == 1);

  CHECK_FALSE(objectDatabase.contains(objectId));
  CHECK(objectDatabase.aliveCount() == baseline);
}

TEST_CASE("asset db: collectGarbage runs to a fixpoint", "[asset][asset_db][gc]") {
  yr::obj::ObjectDB& objectDatabase = yr::obj::globalObjectDB();
  const std::size_t baseline = objectDatabase.aliveCount();

  AssetDatabase database("res://assets");
  const AssetUID textureUid{2};
  const AssetUID materialUid{1};

  auto texture = makeLoaded<Texture>(textureUid, "res://textures/base.png");
  auto material = makeLoaded<Material>(materialUid, "res://materials/base.material");
  material->baseColorTexture = texture;

  REQUIRE(database.store(texture));
  REQUIRE(database.store(material));
  CHECK(texture->refcount() == 3);  // 外部 + Material + DB
  CHECK(material->refcount() == 2); // 外部 + DB

  material.reset();
  texture.reset();
  // raw 指针仍有效：对象由 DB 持有，但这里只读 refcount，不做所有权操作。
  const Texture* textureRaw = database.findAs<Texture>(textureUid).get();
  const Material* materialRaw = database.findAs<Material>(materialUid).get();
  REQUIRE(textureRaw != nullptr);
  REQUIRE(materialRaw != nullptr);
  CHECK(materialRaw->refcount() == 1);
  CHECK(textureRaw->refcount() == 2); // Material 仍持有

  const yr::asset::GcStats gc = database.collectGarbage();
  CHECK(gc.evicted == 2);
  CHECK(gc.passes >= 2);
  CHECK(database.stats().resident_count == 0);
  CHECK(database.stats().cpu_bytes == 0);
  CHECK(objectDatabase.aliveCount() == baseline);
}

TEST_CASE("asset db: evict requires sole DB ownership", "[asset][asset_db][gc]") {
  AssetDatabase database("res://assets");

  auto mesh = makeLoaded<Mesh>(AssetUID{1}, "res://meshes/quad.mesh");
  REQUIRE(database.store(mesh));

  CHECK_FALSE(database.evict(AssetUID{1})); // 外部 Ref 还在
  CHECK(database.contains(AssetUID{1}));

  mesh.reset();
  CHECK(database.evict(AssetUID{1}));
  CHECK_FALSE(database.contains(AssetUID{1}));
  CHECK_FALSE(database.evict(AssetUID{1}));
  CHECK_FALSE(database.evict(AssetUID{99}));

  CHECK(database.stats().resident_count == 0);
  CHECK(database.stats().evicted == 1);
}

TEST_CASE("asset db: stats account resident payload and counters", "[asset][asset_db][stats]") {
  AssetDatabase database("res://assets");

  auto mesh = makeLoaded<Mesh>(AssetUID{1}, "res://meshes/quad.mesh");
  mesh->vertices.resize(3);
  mesh->indices = {0, 1, 2, 2, 3, 0};

  auto material = makeLoaded<Material>(AssetUID{2}, "res://materials/base.material");

  auto texture = makeLoaded<Texture>(AssetUID{3}, "res://textures/white.png");
  texture->width = 2;
  texture->height = 2;
  texture->rgba8.assign(16, 0xFF);

  REQUIRE(database.store(mesh));
  REQUIRE(database.store(material));
  REQUIRE(database.store(texture));

  const std::uint64_t expectedBytes =
      static_cast<std::uint64_t>(3) * sizeof(MeshVertex) + static_cast<std::uint64_t>(6) * sizeof(std::uint32_t) + 16;

  yr::asset::AssetStats stats = database.stats();
  CHECK(stats.resident_count == 3);
  CHECK(stats.cpu_bytes == expectedBytes);
  CHECK(stats.registered == 3);
  CHECK(stats.evicted == 0);

  mesh.reset();
  material.reset();
  texture.reset();
  REQUIRE(database.evict(AssetUID{1}));
  REQUIRE(database.evict(AssetUID{2}));
  REQUIRE(database.evict(AssetUID{3}));

  stats = database.stats();
  CHECK(stats.resident_count == 0);
  CHECK(stats.cpu_bytes == 0);
  CHECK(stats.registered == 3);
  CHECK(stats.evicted == 3);
}

TEST_CASE("asset db: clear drops entries but keeps cumulative counters", "[asset][asset_db]") {
  AssetDatabase database("res://assets");
  auto mesh = makeLoaded<Mesh>(AssetUID{1}, "res://meshes/quad.mesh");
  REQUIRE(database.store(mesh));

  database.clear();
  CHECK_FALSE(database.contains(AssetUID{1}));
  CHECK(database.stats().resident_count == 0);
  CHECK(database.stats().registered == 1);
  CHECK(mesh->refcount() == 1); // clear 只丢弃 DB 引用
}

TEST_CASE("asset db: empty database is safe", "[asset][asset_db]") {
  AssetDatabase database("");

  CHECK(database.assetRoot().empty());
  CHECK_FALSE(database.contains(AssetUID{}));
  CHECK_FALSE(database.find(AssetUID{1}).valid());
  CHECK_FALSE(database.findAs<Mesh>(AssetUID{1}).valid());
  CHECK(database.pathFor(AssetUID{1}).empty());
  CHECK_FALSE(database.evict(AssetUID{1}));

  const yr::asset::GcStats gc = database.collectGarbage();
  CHECK(gc.evicted == 0);
  CHECK(gc.passes == 0);

  const yr::asset::AssetStats stats = database.stats();
  CHECK(stats.resident_count == 0);
  CHECK(stats.cpu_bytes == 0);
  CHECK(stats.registered == 0);
  CHECK(stats.evicted == 0);

  database.clear();
  CHECK(database.stats().resident_count == 0);
}

TEST_CASE("asset db: loadGltfScene reuses resources and tracks stats", "[asset][asset_db][gltf]") {
  yr::obj::ObjectDB& objectDatabase = yr::obj::globalObjectDB();
  const std::size_t baseline = objectDatabase.aliveCount();

  AssetDatabase database(YR_TEST_ASSET_ROOT);
  {
    const GltfLoadResult first = database.loadGltfScene("res://models/ToyCar/ToyCar.gltf");
    CAPTURE(first.error);
    REQUIRE(first.ok);

    const AssetStats firstStats = database.stats();
    CHECK(firstStats.resident_count > 0);
    CHECK(firstStats.loads == 1);
    CHECK(firstStats.failed_loads == 0);

    REQUIRE_FALSE(first.scene.roots.empty());
    REQUIRE_FALSE(first.scene.roots[0].primitives.empty());
    const yr::obj::Ref<Mesh> mesh = first.scene.roots[0].primitives[0].mesh;
    REQUIRE(mesh.valid());

    const AssetUID meshUid = mesh->uid();
    CHECK(meshUid.valid());
    CHECK(database.contains(meshUid));
    CHECK(database.findAs<Mesh>(meshUid).get() == mesh.get());
    CHECK_FALSE(database.pathFor(meshUid).empty());

    const GltfLoadResult second = database.loadGltfScene("res://models/ToyCar/ToyCar.gltf");
    CAPTURE(second.error);
    REQUIRE(second.ok);
    REQUIRE_FALSE(second.scene.roots.empty());
    REQUIRE_FALSE(second.scene.roots[0].primitives.empty());
    CHECK(second.scene.roots[0].primitives[0].mesh.get() == mesh.get());

    const AssetStats secondStats = database.stats();
    CHECK(secondStats.loads == 2);
    CHECK(secondStats.cache_hits > 0);
    CHECK(secondStats.resident_count == firstStats.resident_count);
  }

  const AssetStats resident = database.stats();
  CHECK(resident.resident_count > 0);

  const GcStats gc = database.collectGarbage();
  CHECK(gc.evicted == resident.resident_count);
  CHECK(database.stats().resident_count == 0);
  CHECK(database.stats().cpu_bytes == 0);
  CHECK(objectDatabase.aliveCount() == baseline);
}

TEST_CASE("asset db: synchronous gltf resources report cache state", "[asset][asset_db][gltf]") {
  AssetDatabase database(YR_TEST_ASSET_ROOT);

  const GltfLoadResult loaded = database.loadGltfScene("res://models/ToyCar/ToyCar.gltf");
  CAPTURE(loaded.error);
  REQUIRE(loaded.ok);
  REQUIRE_FALSE(loaded.scene.roots.empty());
  REQUIRE_FALSE(loaded.scene.roots[0].primitives.empty());

  const yr::obj::Ref<Mesh> mesh = loaded.scene.roots[0].primitives[0].mesh;
  REQUIRE(mesh.valid());
  const AssetUID meshUid = mesh->uid();
  REQUIRE(meshUid.valid());

  CHECK(database.state(meshUid) == LoadState::kReady);
  CHECK(database.error(meshUid).empty());
}

TEST_CASE("asset db: loadGltfScene failure leaves cache untouched", "[asset][asset_db][gltf]") {
  AssetDatabase database(YR_TEST_ASSET_ROOT);
  const std::size_t before = database.stats().resident_count;

  const GltfLoadResult result = database.loadGltfScene("res://models/NoSuchFile.gltf");
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.error.empty());

  const AssetStats stats = database.stats();
  CHECK(stats.loads == 1);
  CHECK(stats.failed_loads == 1);
  CHECK(stats.resident_count == before);
}

TEST_CASE("asset db: loadGltfScene defers textures and pump fills placeholders", "[asset][asset_db][gltf][async]") {
  yr::obj::ObjectDB& objectDatabase = yr::obj::globalObjectDB();
  const std::size_t baseline = objectDatabase.aliveCount();

  AssetDatabase database(YR_TEST_ASSET_ROOT);
  {
    const GltfLoadResult loaded = database.loadGltfScene("res://models/ToyCar/ToyCar.gltf", true);
    CAPTURE(loaded.error);
    REQUIRE(loaded.ok);
    REQUIRE_FALSE(loaded.pendingTextures.empty());

    REQUIRE_FALSE(loaded.scene.roots.empty());
    REQUIRE_FALSE(loaded.scene.roots[0].primitives.empty());
    const yr::obj::Ref<Texture> baseColorTexture = loaded.scene.roots[0].primitives[0].material->baseColorTexture;
    REQUIRE(baseColorTexture.valid());
    CHECK(baseColorTexture->state() == LoadState::kLoading);
    CHECK(baseColorTexture->width == 0);
    CHECK(baseColorTexture->height == 0);
    CHECK(database.state(baseColorTexture->uid()) == LoadState::kLoading);
    CHECK(database.findAs<Texture>(baseColorTexture->uid()).get() == baseColorTexture.get());

    REQUIRE(pumpUntilAllReady(database, loaded.pendingTextures));

    for (const yr::asset::GltfTextureRequest& request : loaded.pendingTextures) {
      CHECK(database.state(request.uid) == LoadState::kReady);
      const yr::obj::Ref<Texture> texture = database.findAs<Texture>(request.uid);
      REQUIRE(texture.valid());
      CHECK(texture->state() == LoadState::kReady);
      CHECK(texture->width > 0);
      CHECK(texture->height > 0);
      CHECK(texture->rgba8.size() ==
            static_cast<std::size_t>(texture->width) * texture->height * static_cast<std::size_t>(4));
    }

    CHECK(baseColorTexture->state() == LoadState::kReady);
    CHECK(baseColorTexture->width > 0);
    CHECK(baseColorTexture->height > 0);
    CHECK(database.findAs<Texture>(baseColorTexture->uid()).get() == baseColorTexture.get());

    const AssetStats stats = database.stats();
    CHECK(stats.loads == 1);
    CHECK(stats.failed_loads == 0);
    CHECK(stats.resident_count >= loaded.pendingTextures.size());
  }

  const GcStats gc = database.collectGarbage();
  CHECK(gc.evicted > 0);
  CHECK(database.stats().resident_count == 0);
  CHECK(database.stats().cpu_bytes == 0);
  CHECK(objectDatabase.aliveCount() == baseline);
}