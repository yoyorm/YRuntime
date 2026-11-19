#include "job_load_backend.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <yr/asset/asset_database.h>
#include <yr/asset/asset_uid.h>
#include <yr/asset/async_loader.h>
#include <yr/asset/memory_file_access.h>
#include <yr/asset/resource_types.h>
#include <yr/job/job_system.h>

namespace {

  using yr::asset::AssetDatabase;
  using yr::asset::AssetUID;
  using yr::asset::IFileAccess;
  using yr::asset::JobLoadBackend;
  using yr::asset::LoadCompletion;
  using yr::asset::LoadRequest;
  using yr::asset::LoadState;
  using yr::asset::MemoryFileAccess;
  using yr::asset::ResourceKind;
  using yr::asset::Texture;
  using yr::asset::TextureColorSpace;

  // 1x1 RGBA PNG：filter 0 + 不透明红色像素（与 test_asset_database_async 相同）。
  constexpr std::array<std::uint8_t, 70> kPng1x1 = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
      0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF,
      0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

  std::vector<std::uint8_t> pngBytes() {
    return {kPng1x1.begin(), kPng1x1.end()};
  }

  std::vector<std::uint8_t> bytesOf(std::string_view text) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size());
    for (const char c : text) {
      bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return bytes;
  }

  // 记录并发 readFile 数量，用来验证 in-flight 上限（既要有并行，又不能超上限）。
  class CountingAccess final : public IFileAccess {
  public:
    CountingAccess(std::unique_ptr<IFileAccess> inner, std::chrono::milliseconds delay)
        : inner_(std::move(inner)), delay_(delay) {}

    bool readFile(std::string_view path, std::vector<std::uint8_t>& out, std::string& error) const override {
      const int now = active_.fetch_add(1, std::memory_order_acq_rel) + 1;
      int previous = max_active_.load(std::memory_order_acquire);
      while (now > previous && !max_active_.compare_exchange_weak(previous, now, std::memory_order_acq_rel)) {
      }
      std::this_thread::sleep_for(delay_);
      const bool ok = inner_->readFile(path, out, error);
      active_.fetch_sub(1, std::memory_order_acq_rel);
      return ok;
    }

    [[nodiscard]] int maxActive() const noexcept { return max_active_.load(std::memory_order_acquire); }

  private:
    std::unique_ptr<IFileAccess> inner_;
    std::chrono::milliseconds delay_;
    mutable std::atomic<int> active_{0};
    mutable std::atomic<int> max_active_{0};
  };

  std::vector<LoadCompletion> collect(JobLoadBackend& worker, std::size_t count,
                                      std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
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

  bool pumpUntilReady(AssetDatabase& database, AssetUID uid,
                      std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      database.pump(1);
      if (database.state(uid) == LoadState::kReady) {
        return true;
      }
      if (database.state(uid) == LoadState::kFailed) {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    database.pump(1);
    return database.state(uid) == LoadState::kReady;
  }

} // namespace

// 纹理解码必须发生在 worker：completion 直接携带 RGBA 像素。
TEST_CASE("async decode: texture completion carries decoded pixels", "[asset][async_decode]") {
  MemoryFileAccess memory;
  memory.addFile("res://pixel.png", pngBytes());

  yr::job::JobSystem jobs(2);
  JobLoadBackend worker(memory, jobs);
  worker.submit(LoadRequest{AssetUID{1}, "res://pixel.png", ResourceKind::kTexture, TextureColorSpace::kSrgb, 0});

  const std::vector<LoadCompletion> completions = collect(worker, 1);
  REQUIRE(completions.size() == 1);
  CHECK(completions[0].error.empty());
  CHECK(completions[0].decoded.width == 1);
  CHECK(completions[0].decoded.height == 1);
  REQUIRE(completions[0].decoded.rgba8.size() == 4);
  CHECK(completions[0].decoded.rgba8[0] == 0xFF); // red
  CHECK(completions[0].decoded.rgba8[3] == 0xFF); // alpha
}

// 解码失败必须在 worker 内表现为 error，而不是把坏字节交给主线程。
TEST_CASE("async decode: bad texture reports decode error", "[asset][async_decode]") {
  MemoryFileAccess memory;
  memory.addFile("res://bad.png", bytesOf("not a png"));

  yr::job::JobSystem jobs(2);
  JobLoadBackend worker(memory, jobs);
  worker.submit(LoadRequest{AssetUID{2}, "res://bad.png", ResourceKind::kTexture, TextureColorSpace::kSrgb, 0});

  const std::vector<LoadCompletion> completions = collect(worker, 1);
  REQUIRE(completions.size() == 1);
  CHECK_FALSE(completions[0].error.empty());
  CHECK(completions[0].decoded.width == 0);
}

// in-flight 上限：读操作既要并行（>=2），又不能超过构造时给的 maxInFlight（=4）。
TEST_CASE("async decode: in-flight reads are capped", "[asset][async_decode]") {
  auto memory = std::make_unique<MemoryFileAccess>();
  memory->addFile("res://pixel.png", pngBytes());
  CountingAccess access(std::move(memory), std::chrono::milliseconds(10));

  constexpr std::size_t kRequests = 16;
  yr::job::JobSystem jobs(4);
  JobLoadBackend worker(access, jobs, 4);

  for (std::size_t index = 0; index < kRequests; ++index) {
    worker.submit(LoadRequest{AssetUID{static_cast<std::uint64_t>(index + 1)}, "res://pixel.png",
                              ResourceKind::kTexture, TextureColorSpace::kSrgb, 0});
  }

  const std::vector<LoadCompletion> completions = collect(worker, kRequests, std::chrono::seconds(10));
  REQUIRE(completions.size() == kRequests);
  CHECK(access.maxActive() >= 2);
  CHECK(access.maxActive() <= 4);
}

// 0 worker 内联模式：task 在 submit 调用线程执行，仍然要完成并带回像素。
TEST_CASE("async decode: zero-worker JobSystem still completes", "[asset][async_decode]") {
  MemoryFileAccess memory;
  memory.addFile("res://pixel.png", pngBytes());

  yr::job::JobSystem jobs(0);
  JobLoadBackend worker(memory, jobs);
  worker.submit(LoadRequest{AssetUID{3}, "res://pixel.png", ResourceKind::kTexture, TextureColorSpace::kSrgb, 0});

  const std::vector<LoadCompletion> completions = collect(worker, 1);
  REQUIRE(completions.size() == 1);
  CHECK(completions[0].error.empty());
  CHECK(completions[0].decoded.width == 1);
}

// AssetDatabase 端到端：pump 只负责安装，像素与同步解码一致。
TEST_CASE("async decode: pump installs worker-decoded pixels", "[asset][async_decode]") {
  auto memory = std::make_unique<MemoryFileAccess>();
  memory->addFile("res://pixel.png", pngBytes());

  yr::job::JobSystem jobs(2);
  AssetDatabase database(std::move(memory), &jobs);

  const AssetUID uid = database.requestLoad("res://pixel.png");
  REQUIRE(uid.valid());
  REQUIRE(pumpUntilReady(database, uid));

  yr::obj::Ref<Texture> texture = database.findAs<Texture>(uid);
  REQUIRE(texture.valid());
  CHECK(texture->width == 1);
  CHECK(texture->height == 1);
  REQUIRE(texture->rgba8.size() == 4);
  CHECK(texture->rgba8[0] == 0xFF);
  CHECK(texture->rgba8[3] == 0xFF);
}
// 完成队列字节预算：预算满时停止提交，takeCompleted 取走后才继续。
// 用 JobSystem(0) 内联执行，让"提交→完成"在同一线程内确定发生。
TEST_CASE("async decode: byte budget stalls submissions", "[asset][async_decode]") {
  MemoryFileAccess memory;
  memory.addFile("res://pixel.png", pngBytes());

  yr::job::JobSystem jobs(0);
  JobLoadBackend worker(memory, jobs, 4, 4); // 预算 = 一个 1x1 纹素的 4 字节

  for (std::size_t index = 0; index < 4; ++index) {
    worker.submit(LoadRequest{AssetUID{static_cast<std::uint64_t>(index + 1)}, "res://pixel.png",
                              ResourceKind::kTexture, TextureColorSpace::kSrgb, 0});
  }

  std::size_t total = 0;
  for (int round = 0; round < 4; ++round) {
    const std::vector<LoadCompletion> batch = worker.takeCompleted(10);
    CHECK(batch.size() == 1); // 预算只够驻留一个完成项
    total += batch.size();
  }

  CHECK(total == 4);
  CHECK(worker.takeCompleted(10).empty());
}

// 预算 0 表示不限制（保持改造前行为）。
TEST_CASE("async decode: zero byte budget means unlimited", "[asset][async_decode]") {
  MemoryFileAccess memory;
  memory.addFile("res://pixel.png", pngBytes());

  yr::job::JobSystem jobs(0);
  JobLoadBackend worker(memory, jobs, 4, 0);

  for (std::size_t index = 0; index < 4; ++index) {
    worker.submit(LoadRequest{AssetUID{static_cast<std::uint64_t>(index + 1)}, "res://pixel.png",
                              ResourceKind::kTexture, TextureColorSpace::kSrgb, 0});
  }

  CHECK(worker.takeCompleted(10).size() == 4);
}
