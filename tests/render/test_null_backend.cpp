#include <catch2/catch_test_macros.hpp>

#include <yr/render/null_backend.h>
#include <yr/render/snapshot.h>
#include <yr/render/snapshot_io.h>

#include <cstddef>
#include <cstdint>
#include <string>

using yr::render::BackendConfig;
using yr::render::BackendStats;
using yr::render::DrawItem;
using yr::render::RenderSnapshot;

namespace {

  RenderSnapshot makeSnapshot(std::size_t itemCount) {
    RenderSnapshot snapshot;
    snapshot.frame_index = 7;
    for (std::size_t index = 0; index < itemCount; ++index) {
      DrawItem item;
      item.object.value = index + 1;
      snapshot.items.push_back(item);
    }
    yr::render::MeshUpload mesh;
    mesh.mesh.value = 11;
    mesh.vertices.resize(2);
    mesh.indices = {0, 1, 0};
    snapshot.uploads.meshes.push_back(mesh);
    yr::render::TextureUpload texture;
    texture.texture.value = 12;
    texture.width = 1;
    texture.height = 1;
    texture.rgba8 = {1, 2, 3, 4};
    snapshot.uploads.textures.push_back(texture);
    yr::render::MaterialUpload material;
    material.material.value = 13;
    snapshot.uploads.materials.push_back(material);
    snapshot.releases.meshes = {{21}};
    snapshot.releases.materials = {{22}};
    snapshot.releases.textures = {{23}};
    return snapshot;
  }

} // namespace

TEST_CASE("null backend: factory returns a named backend", "[render][null_backend]") {
  const auto backend = yr::render::create_null_backend();

  REQUIRE(backend != nullptr);
  CHECK(std::string(backend->name()) == "null");
}

TEST_CASE("null backend: stats reflect the consumed snapshot", "[render][null_backend]") {
  auto backend = yr::render::create_null_backend();
  REQUIRE(backend->initialize(BackendConfig{}));

  const RenderSnapshot snapshot = makeSnapshot(3);
  backend->present(snapshot);

  const BackendStats stats = backend->last_frame_stats();
  CHECK(stats.items == 3);
  CHECK(stats.draw_calls == 3);
  CHECK(stats.triangles == 3);
  CHECK(stats.snapshot_bytes == yr::render::snapshotByteSize(snapshot));
  CHECK(stats.uploaded_assets == 3);
  CHECK(stats.released_assets == 3);
  CHECK(stats.missing_assets == 0);
  CHECK(stats.upload_bytes == 2 * 32 + 3 * 4 + 4);
}

TEST_CASE("null backend: same input yields identical stats", "[render][null_backend]") {
  auto backend = yr::render::create_null_backend();
  REQUIRE(backend->initialize(BackendConfig{}));

  const RenderSnapshot snapshot = makeSnapshot(5);
  backend->present(snapshot);
  const BackendStats first = backend->last_frame_stats();
  backend->present(snapshot);
  const BackendStats second = backend->last_frame_stats();

  CHECK(first.items == second.items);
  CHECK(first.draw_calls == second.draw_calls);
  CHECK(first.triangles == second.triangles);
  CHECK(first.snapshot_bytes == second.snapshot_bytes);
  CHECK(first.uploaded_assets == second.uploaded_assets);
  CHECK(first.released_assets == second.released_assets);
  CHECK(first.missing_assets == second.missing_assets);
  CHECK(first.upload_bytes == second.upload_bytes);
}

TEST_CASE("null backend: empty snapshot yields zero draw stats", "[render][null_backend]") {
  auto backend = yr::render::create_null_backend();
  REQUIRE(backend->initialize(BackendConfig{}));

  backend->present(RenderSnapshot{});

  const BackendStats stats = backend->last_frame_stats();
  CHECK(stats.items == 0);
  CHECK(stats.draw_calls == 0);
  CHECK(stats.triangles == 0);
  CHECK(stats.uploaded_assets == 0);
  CHECK(stats.released_assets == 0);
  CHECK(stats.missing_assets == 0);
  CHECK(stats.upload_bytes == 0);
  CHECK(stats.snapshot_bytes == yr::render::snapshotByteSize(RenderSnapshot{}));
}

TEST_CASE("null backend: shutdown disables the backend", "[render][null_backend]") {
  auto backend = yr::render::create_null_backend();
  REQUIRE(backend->initialize(BackendConfig{}));
  backend->shutdown();

  CHECK(std::string(backend->name()) == "null");
}
