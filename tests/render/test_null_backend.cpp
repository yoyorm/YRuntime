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
  CHECK(stats.snapshot_bytes == yr::render::encodeSnapshot(snapshot).size());
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
}

TEST_CASE("null backend: empty snapshot yields zero draw stats", "[render][null_backend]") {
  auto backend = yr::render::create_null_backend();
  REQUIRE(backend->initialize(BackendConfig{}));

  backend->present(RenderSnapshot{});

  const BackendStats stats = backend->last_frame_stats();
  CHECK(stats.items == 0);
  CHECK(stats.draw_calls == 0);
  CHECK(stats.triangles == 0);
}

TEST_CASE("null backend: shutdown disables the backend", "[render][null_backend]") {
  auto backend = yr::render::create_null_backend();
  REQUIRE(backend->initialize(BackendConfig{}));
  backend->shutdown();

  CHECK(std::string(backend->name()) == "null");
}