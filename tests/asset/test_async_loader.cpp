#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <yr/asset/asset_uid.h>
#include <yr/asset/async_loader.h>
#include <yr/asset/resource.h>

namespace {

  using yr::asset::AssetUID;
  using yr::asset::AsyncLoader;
  using yr::asset::ILoadBackend;
  using yr::asset::LoadCompletion;
  using yr::asset::LoadRequest;
  using yr::asset::LoadState;
  using yr::asset::ResourceKind;
  using yr::asset::TextureColorSpace;

  class FakeLoadBackend final : public ILoadBackend {
  public:
    void submit(LoadRequest request) override { submitted.push_back(std::move(request)); }

    std::vector<LoadCompletion> takeCompleted(std::size_t maxCount) override {
      std::vector<LoadCompletion> out;
      while (!completed.empty() && out.size() < maxCount) {
        out.push_back(std::move(completed.front()));
        completed.pop_front();
      }
      return out;
    }

    void complete(AssetUID uid, std::vector<std::uint8_t> payload,
                  TextureColorSpace colorSpace = TextureColorSpace::kSrgb, std::uint32_t maxDimension = 0) {
      completed.push_back(
          LoadCompletion{uid, {}, ResourceKind::kUnknown, std::move(payload), {}, colorSpace, maxDimension});
    }

    void fail(AssetUID uid, std::string error) {
      completed.push_back(LoadCompletion{uid, {}, ResourceKind::kUnknown, {}, std::move(error)});
    }

    std::vector<LoadRequest> submitted;
    std::deque<LoadCompletion> completed;
  };

  std::vector<std::uint8_t> bytesOf(std::string_view text) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size());
    for (const char c : text) {
      bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return bytes;
  }

} // namespace

TEST_CASE("async loader: first request enters loading and submits once", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const char* const path = "res://textures/first.png";
  const AssetUID uid = loader.requestLoad(path, ResourceKind::kTexture);

  REQUIRE(uid.valid());
  CHECK(uid == AssetUID::fromPath(path));
  CHECK(loader.state(uid) == LoadState::kLoading);
  CHECK(loader.error(uid).empty());
  REQUIRE(backend.submitted.size() == 1);
  CHECK(backend.submitted[0].uid == uid);
  CHECK(backend.submitted[0].resPath == path);
  CHECK(backend.submitted[0].kind == ResourceKind::kTexture);
}

TEST_CASE("async loader: request carries texture color space and dimension cap", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID uid =
      loader.requestLoad("res://textures/linear.png", ResourceKind::kTexture, TextureColorSpace::kLinear, 512);

  REQUIRE(uid.valid());
  REQUIRE(backend.submitted.size() == 1);
  CHECK(backend.submitted[0].colorSpace == TextureColorSpace::kLinear);
  CHECK(backend.submitted[0].maxDimension == 512);
}

TEST_CASE("async loader: completion passes through color space and dimension cap", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID uid =
      loader.requestLoad("res://textures/normal.png", ResourceKind::kTexture, TextureColorSpace::kLinear, 256);
  backend.complete(uid, bytesOf("rgba"), TextureColorSpace::kLinear, 256);

  const std::vector<LoadCompletion> completions = loader.pump();
  REQUIRE(completions.size() == 1);
  CHECK(completions[0].colorSpace == TextureColorSpace::kLinear);
  CHECK(completions[0].maxDimension == 256);
}

TEST_CASE("async loader: repeated request for same path is merged", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID first = loader.requestLoad("res://textures/merged.png");
  const AssetUID second = loader.requestLoad("res://textures/merged.png");

  CHECK(first == second);
  CHECK(backend.submitted.size() == 1);
}

TEST_CASE("async loader: successful completion moves state to ready", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID uid = loader.requestLoad("res://textures/ready.png");
  const std::vector<std::uint8_t> payload = bytesOf("rgba");
  backend.complete(uid, payload);

  const std::vector<LoadCompletion> completions = loader.pump();
  REQUIRE(completions.size() == 1);
  CHECK(completions[0].uid == uid);
  CHECK(completions[0].payload == payload);
  CHECK(completions[0].error.empty());
  CHECK(loader.state(uid) == LoadState::kReady);
  CHECK(loader.error(uid).empty());
}

TEST_CASE("async loader: failed completion stores error and state", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID uid = loader.requestLoad("res://textures/broken.png");
  backend.fail(uid, "decode failed");

  const std::vector<LoadCompletion> completions = loader.pump();
  REQUIRE(completions.size() == 1);
  CHECK(completions[0].uid == uid);
  CHECK(completions[0].error == "decode failed");
  CHECK(loader.state(uid) == LoadState::kFailed);
  CHECK(loader.error(uid) == "decode failed");
}

TEST_CASE("async loader: failed request can be retried", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID uid = loader.requestLoad("res://textures/retry.png");
  backend.fail(uid, "io error");
  REQUIRE(loader.pump().size() == 1);
  REQUIRE(loader.state(uid) == LoadState::kFailed);

  const AssetUID retried = loader.requestLoad("res://textures/retry.png");
  CHECK(retried == uid);
  CHECK(loader.state(uid) == LoadState::kLoading);
  CHECK(loader.error(uid).empty());
  CHECK(backend.submitted.size() == 2);
}

TEST_CASE("async loader: ready request is not submitted again", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID uid = loader.requestLoad("res://textures/cached.png");
  backend.complete(uid, bytesOf("cached"));
  REQUIRE(loader.pump().size() == 1);
  REQUIRE(loader.state(uid) == LoadState::kReady);

  const AssetUID again = loader.requestLoad("res://textures/cached.png");
  CHECK(again == uid);
  CHECK(loader.state(uid) == LoadState::kReady);
  CHECK(backend.submitted.size() == 1);
}

TEST_CASE("async loader: pump honors completion budget", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID first = loader.requestLoad("res://textures/budget-a.png");
  const AssetUID second = loader.requestLoad("res://textures/budget-b.png");
  const AssetUID third = loader.requestLoad("res://textures/budget-c.png");
  backend.complete(first, bytesOf("a"));
  backend.complete(second, bytesOf("b"));
  backend.complete(third, bytesOf("c"));

  const std::vector<LoadCompletion> one = loader.pump(1);
  REQUIRE(one.size() == 1);
  CHECK(one[0].uid == first);
  CHECK(backend.completed.size() == 2);
  CHECK(loader.state(first) == LoadState::kReady);
  CHECK(loader.state(second) == LoadState::kLoading);

  const std::vector<LoadCompletion> rest = loader.pump(5);
  REQUIRE(rest.size() == 2);
  CHECK(rest[0].uid == second);
  CHECK(rest[1].uid == third);
  CHECK(loader.state(second) == LoadState::kReady);
  CHECK(loader.state(third) == LoadState::kReady);
}

TEST_CASE("async loader: unknown completion is dropped", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  backend.complete(AssetUID{0xDEADBEEFULL}, bytesOf("late"));

  CHECK(loader.pump().empty());
  CHECK(loader.state(AssetUID{0xDEADBEEFULL}) == LoadState::kUnloaded);
}

TEST_CASE("async loader: duplicate completion is ignored", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID uid = loader.requestLoad("res://textures/duplicate.png");
  backend.complete(uid, bytesOf("first"));
  backend.complete(uid, bytesOf("second"));

  const std::vector<LoadCompletion> completions = loader.pump(2);
  REQUIRE(completions.size() == 1);
  CHECK(completions[0].payload == bytesOf("first"));
  CHECK(loader.state(uid) == LoadState::kReady);
}

TEST_CASE("async loader: forget allows resubmission", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID uid = loader.requestLoad("res://textures/forget.png");
  REQUIRE(backend.submitted.size() == 1);

  loader.forget(uid);
  CHECK(loader.state(uid) == LoadState::kUnloaded);
  CHECK(loader.error(uid).empty());

  const AssetUID again = loader.requestLoad("res://textures/forget.png");
  CHECK(again == uid);
  CHECK(loader.state(uid) == LoadState::kLoading);
  CHECK(backend.submitted.size() == 2);
}

TEST_CASE("async loader: markFailed turns ready into failed", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID uid = loader.requestLoad("res://textures/install.png");
  backend.complete(uid, bytesOf("data"));
  REQUIRE(loader.pump().size() == 1);
  REQUIRE(loader.state(uid) == LoadState::kReady);

  loader.markFailed(uid, "cache install failed");
  CHECK(loader.state(uid) == LoadState::kFailed);
  CHECK(loader.error(uid) == "cache install failed");

  loader.markFailed(AssetUID{0x1234}, "unknown");
  CHECK(loader.state(AssetUID{0x1234}) == LoadState::kUnloaded);
}

TEST_CASE("async loader: invalid path is rejected without submit", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  CHECK_FALSE(loader.requestLoad("textures/missing-prefix.png").valid());
  CHECK_FALSE(loader.requestLoad("").valid());
  CHECK(backend.submitted.empty());
}

TEST_CASE("async loader: unknown uid state and error are safe", "[asset][async_loader]") {
  FakeLoadBackend backend;
  AsyncLoader loader(backend);

  const AssetUID unknown{0xABCDEFULL};
  CHECK(loader.state(unknown) == LoadState::kUnloaded);
  CHECK(loader.error(unknown).empty());
  CHECK(loader.pump(0).empty());
}
