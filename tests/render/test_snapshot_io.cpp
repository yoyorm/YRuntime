#include <catch2/catch_test_macros.hpp>

#include <yr/render/snapshot.h>
#include <yr/render/snapshot_io.h>

#include <cstddef>
#include <cstdint>
#include <vector>

using yr::render::DrawItem;
using yr::render::RenderSnapshot;
using yr::render::TransformPod;

namespace {

  bool sameTransform(const TransformPod& lhs, const TransformPod& rhs) {
    for (std::size_t index = 0; index < 16; ++index) {
      if (lhs.m[index] != rhs.m[index]) {
        return false;
      }
    }
    return true;
  }

  bool sameItem(const DrawItem& lhs, const DrawItem& rhs) {
    return lhs.object == rhs.object && sameTransform(lhs.world, rhs.world) && lhs.mesh == rhs.mesh &&
           lhs.material == rhs.material;
  }

  bool sameSnapshot(const RenderSnapshot& lhs, const RenderSnapshot& rhs) {
    if (lhs.frame_index != rhs.frame_index || !sameTransform(lhs.camera.view, rhs.camera.view) ||
        !sameTransform(lhs.camera.proj, rhs.camera.proj) || lhs.items.size() != rhs.items.size()) {
      return false;
    }
    for (std::size_t index = 0; index < lhs.items.size(); ++index) {
      if (!sameItem(lhs.items[index], rhs.items[index])) {
        return false;
      }
    }
    return true;
  }

  RenderSnapshot makeSnapshot() {
    RenderSnapshot snapshot;
    snapshot.frame_index = 12345;
    for (std::size_t index = 0; index < 16; ++index) {
      snapshot.camera.view.m[index] = static_cast<float>(index);
      snapshot.camera.proj.m[index] = static_cast<float>(index) * 0.5F;
    }
    for (std::uint64_t index = 0; index < 3; ++index) {
      DrawItem item;
      item.object.value = 100 + index;
      item.mesh.value = 200 + index;
      item.material.value = 300 + index;
      for (std::size_t element = 0; element < 16; ++element) {
        item.world.m[element] = static_cast<float>(index * 16 + element);
      }
      snapshot.items.push_back(item);
    }
    return snapshot;
  }

} // namespace

TEST_CASE("snapshot io: round-trip preserves every field", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();

  const std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  RenderSnapshot decoded;
  REQUIRE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), decoded));

  CHECK(sameSnapshot(original, decoded));
}

TEST_CASE("snapshot io: encoding is byte-for-byte deterministic", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();

  const std::vector<std::uint8_t> first = yr::render::encodeSnapshot(original);
  const std::vector<std::uint8_t> second = yr::render::encodeSnapshot(original);

  CHECK(first == second);
}

TEST_CASE("snapshot io: tampered magic is rejected and out untouched", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();
  std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  encoded[0] = static_cast<std::uint8_t>(encoded[0] ^ 0xFFU);
  const RenderSnapshot sentinel = makeSnapshot();

  RenderSnapshot out = sentinel;
  CHECK_FALSE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), out));
  CHECK(sameSnapshot(out, sentinel));
}

TEST_CASE("snapshot io: tampered version is rejected and out untouched", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();
  std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  encoded[6] = static_cast<std::uint8_t>(encoded[6] ^ 0xFFU);
  const RenderSnapshot sentinel = makeSnapshot();

  RenderSnapshot out = sentinel;
  CHECK_FALSE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), out));
  CHECK(sameSnapshot(out, sentinel));
}

TEST_CASE("snapshot io: truncated buffer is rejected and out untouched", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();
  const std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  const std::vector<std::uint8_t> truncated(encoded.begin(), encoded.end() - 1);
  const RenderSnapshot sentinel = makeSnapshot();

  RenderSnapshot out = sentinel;
  CHECK_FALSE(yr::render::decodeSnapshot(truncated.data(), truncated.size(), out));
  CHECK(sameSnapshot(out, sentinel));
}

TEST_CASE("snapshot io: trailing byte is rejected and out untouched", "[render][snapshot_io]") {
  const RenderSnapshot original = makeSnapshot();
  std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  encoded.push_back(0);
  const RenderSnapshot sentinel = makeSnapshot();

  RenderSnapshot out = sentinel;
  CHECK_FALSE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), out));
  CHECK(sameSnapshot(out, sentinel));
}

TEST_CASE("snapshot io: empty snapshot round-trips", "[render][snapshot_io]") {
  const RenderSnapshot original{};

  const std::vector<std::uint8_t> encoded = yr::render::encodeSnapshot(original);
  RenderSnapshot decoded;
  REQUIRE(yr::render::decodeSnapshot(encoded.data(), encoded.size(), decoded));

  CHECK(decoded.frame_index == 0);
  CHECK(decoded.items.empty());
}