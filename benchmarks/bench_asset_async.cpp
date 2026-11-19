// E10 · 资产异步加载：慢 IO 下主线程 pump 的帧开销
//
// 场景：ThrottledFileAccess(50 ms) × 20 个纹理请求。
// 目标（doc/tasks/m10-asset-worker.md §6）：主线程 pump 的 p95 < 5 ms，全部就绪约 1 s。
//
// 这是手动运行的性能实验（不注册到 ctest），用来看"解码/IO 搬到 worker 之后，
// 主线程每帧还剩多少活"。对比 1 个 worker 和 4 个 worker 的总耗时。
//
// 用法：
//   cmake --preset benchmark && cmake --build --preset benchmark
//   ./build/benchmark/benchmarks/YrBench_asset_async

#include <yr/asset/asset_database.h>
#include <yr/asset/asset_uid.h>
#include <yr/asset/memory_file_access.h>
#include <yr/asset/resource_types.h>
#include <yr/asset/throttled_file_access.h>
#include <yr/job/job_system.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

  using Clock = std::chrono::steady_clock;

  constexpr std::size_t kTextures = 20;
  constexpr std::chrono::milliseconds kIoLatency{50};
  constexpr std::chrono::milliseconds kDeadline{5000};

  // 1x1 RGBA PNG（红色、不透明）：解码成本可忽略，瓶颈完全在 IO。
  constexpr std::array<std::uint8_t, 70> kPng1x1 = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
      0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF,
      0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

  std::string texturePath(std::size_t index) {
    std::string path = "res://tex";
    path += std::to_string(index);
    path += ".png";
    return path;
  }

  double percentile(std::vector<double> values, double ratio) {
    if (values.empty()) {
      return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(static_cast<double>(values.size() - 1) * ratio);
    return values[index];
  }

  double millis(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  }

  void run_case(std::size_t workers) {
    yr::job::JobSystem jobs(workers);

    auto memory = std::make_unique<yr::asset::MemoryFileAccess>();
    for (std::size_t index = 0; index < kTextures; ++index) {
      memory->addFile(texturePath(index), {kPng1x1.begin(), kPng1x1.end()});
    }
    auto throttled = std::make_unique<yr::asset::ThrottledFileAccess>(std::move(memory), kIoLatency);

    yr::asset::AssetDatabase database(std::move(throttled), &jobs);

    std::vector<yr::asset::AssetUID> uids;
    uids.reserve(kTextures);
    const auto start = Clock::now();
    for (std::size_t index = 0; index < kTextures; ++index) {
      uids.push_back(database.requestLoad(texturePath(index), yr::asset::ResourceKind::kTexture));
    }

    std::vector<double> pumpMillis;
    std::size_t ready = 0;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start) < kDeadline) {
      const auto pumpStart = Clock::now();
      database.pump(1);
      pumpMillis.push_back(millis(Clock::now() - pumpStart));

      ready = 0;
      for (const yr::asset::AssetUID uid : uids) {
        if (database.state(uid) == yr::asset::LoadState::kReady) {
          ++ready;
        }
      }
      if (ready == kTextures) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 模拟帧间隔
    }

    const double totalMs = millis(Clock::now() - start);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "workers=" << workers << "  ready=" << ready << "/" << kTextures << "  total=" << totalMs << " ms"
              << "  pump: calls=" << pumpMillis.size() << "  p50=" << percentile(pumpMillis, 0.50)
              << " ms  p95=" << percentile(pumpMillis, 0.95) << " ms  max=" << percentile(pumpMillis, 1.0) << " ms"
              << std::endl;

    const double p95 = percentile(pumpMillis, 0.95);
    if (p95 >= 5.0) {
      std::cout << "  [warn] pump p95 超过 E10 目标 5 ms" << std::endl;
    }
  }

} // namespace

int main() {
  std::cout << "E10 asset async: " << kTextures << " textures, IO latency " << kIoLatency.count() << " ms per read"
            << std::endl;
  run_case(1);
  run_case(4);
  return 0;
}