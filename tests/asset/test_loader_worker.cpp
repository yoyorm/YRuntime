#include "loader_worker.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <yr/asset/asset_uid.h>
#include <yr/asset/async_loader.h>
#include <yr/asset/memory_file_access.h>
#include <yr/asset/resource.h>
#include <yr/asset/throttled_file_access.h>

namespace {

  using yr::asset::AssetUID;
  using yr::asset::LoadCompletion;
  using yr::asset::LoaderWorker;
  using yr::asset::LoadRequest;
  using yr::asset::MemoryFileAccess;
  using yr::asset::ResourceKind;
  using yr::asset::ThrottledFileAccess;

  std::vector<std::uint8_t> bytesOf(std::string_view text) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size());
    for (const char c : text) {
      bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return bytes;
  }

  std::vector<LoadCompletion> collect(LoaderWorker& worker, std::size_t count,
                                      std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    std::vector<LoadCompletion> out;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (out.size() < count && std::chrono::steady_clock::now() < deadline) {
      std::vector<LoadCompletion> batch = worker.takeCompleted(count - out.size());
      out.insert(out.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
      if (out.size() < count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    return out;
  }

} // namespace

TEST_CASE("loader worker: reads file on worker thread", "[asset][loader_worker]") {
  MemoryFileAccess memory;
  memory.addFile("res://data.bin", bytesOf("payload"));

  LoaderWorker worker(memory);
  worker.submit(LoadRequest{AssetUID{1}, "res://data.bin", ResourceKind::kTexture});

  const std::vector<LoadCompletion> completions = collect(worker, 1);
  REQUIRE(completions.size() == 1);
  CHECK(completions[0].uid == AssetUID{1});
  CHECK(completions[0].kind == ResourceKind::kTexture);
  CHECK(completions[0].payload == bytesOf("payload"));
  CHECK(completions[0].error.empty());
}

TEST_CASE("loader worker: missing file reports error", "[asset][loader_worker]") {
  MemoryFileAccess memory;

  LoaderWorker worker(memory);
  worker.submit(LoadRequest{AssetUID{2}, "res://data/missing.bin", ResourceKind::kMesh});

  const std::vector<LoadCompletion> completions = collect(worker, 1);
  REQUIRE(completions.size() == 1);
  CHECK(completions[0].uid == AssetUID{2});
  CHECK(completions[0].kind == ResourceKind::kMesh);
  CHECK_FALSE(completions[0].error.empty());
  CHECK(completions[0].payload.empty());
}

TEST_CASE("loader worker: multiple requests complete", "[asset][loader_worker]") {
  MemoryFileAccess memory;
  memory.addFile("res://data/a.bin", bytesOf("a"));
  memory.addFile("res://data/b.bin", bytesOf("b"));
  memory.addFile("res://data/c.bin", bytesOf("c"));

  LoaderWorker worker(memory);
  worker.submit(LoadRequest{AssetUID{10}, "res://data/a.bin", ResourceKind::kUnknown});
  worker.submit(LoadRequest{AssetUID{11}, "res://data/b.bin", ResourceKind::kUnknown});
  worker.submit(LoadRequest{AssetUID{12}, "res://data/c.bin", ResourceKind::kUnknown});

  const std::vector<LoadCompletion> completions = collect(worker, 3);
  REQUIRE(completions.size() == 3);

  std::set<std::uint64_t> uids;
  for (const LoadCompletion& completion : completions) {
    CHECK(completion.error.empty());
    uids.insert(completion.uid.value);
  }
  CHECK(uids == std::set<std::uint64_t>{10, 11, 12});
}

TEST_CASE("loader worker: take completed zero returns empty", "[asset][loader_worker]") {
  MemoryFileAccess memory;
  memory.addFile("res://data.bin", bytesOf("payload"));

  LoaderWorker worker(memory);
  worker.submit(LoadRequest{AssetUID{3}, "res://data.bin", ResourceKind::kUnknown});
  REQUIRE(collect(worker, 1).size() == 1);

  CHECK(worker.takeCompleted(0).empty());
}

TEST_CASE("loader worker: destruction with pending request", "[asset][loader_worker]") {
  MemoryFileAccess memory;
  memory.addFile("res://data.bin", bytesOf("payload"));

  LoaderWorker worker(memory);
  worker.submit(LoadRequest{AssetUID{4}, "res://data.bin", ResourceKind::kUnknown});
}

TEST_CASE("loader worker: read runs off the submitting thread", "[asset][loader_worker]") {
  auto memory = std::make_unique<MemoryFileAccess>();
  memory->addFile("res://data/slow.bin", bytesOf("slow"));
  ThrottledFileAccess access(std::move(memory), std::chrono::milliseconds(20));

  LoaderWorker worker(access);
  const auto start = std::chrono::steady_clock::now();
  worker.submit(LoadRequest{AssetUID{5}, "res://data/slow.bin", ResourceKind::kUnknown});

  CHECK(worker.takeCompleted(1).empty());

  const std::vector<LoadCompletion> completions = collect(worker, 1);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  REQUIRE(completions.size() == 1);
  CHECK(completions[0].payload == bytesOf("slow"));
  CHECK(completions[0].error.empty());
  CHECK(elapsed >= std::chrono::milliseconds(10));
}

TEST_CASE("loader worker: duplicate requests are not merged", "[asset][loader_worker]") {
  MemoryFileAccess memory;
  memory.addFile("res://data/dup.bin", bytesOf("dup"));

  LoaderWorker worker(memory);
  worker.submit(LoadRequest{AssetUID{6}, "res://data/dup.bin", ResourceKind::kUnknown});
  worker.submit(LoadRequest{AssetUID{6}, "res://data/dup.bin", ResourceKind::kUnknown});

  const std::vector<LoadCompletion> completions = collect(worker, 2);
  REQUIRE(completions.size() == 2);
  CHECK(completions[0].uid == AssetUID{6});
  CHECK(completions[1].uid == AssetUID{6});
}
