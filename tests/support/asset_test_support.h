#pragma once

#include <catch2/catch_test_macros.hpp>

#include <yr/asset/async_loader.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <thread>
#include <vector>

namespace yr_test {

  // 1x1 RGBA PNG：filter 0 + 不透明红色像素。
  inline constexpr std::array<std::uint8_t, 70> kPng1x1 = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
      0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF,
      0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

  inline std::vector<std::uint8_t> pngBytes() {
    return {kPng1x1.begin(), kPng1x1.end()};
  }

  inline std::vector<std::uint8_t> bytesOf(std::string_view text) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size());
    for (const char c : text) {
      bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return bytes;
  }

  // 轮询取完成项，直到数量满足或超时；worker 类型由调用方提供。
  template <typename Backend>
  std::vector<yr::asset::LoadCompletion> collect(Backend& worker, std::size_t count,
                                                 std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    std::vector<yr::asset::LoadCompletion> out;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (out.size() < count && std::chrono::steady_clock::now() < deadline) {
      std::vector<yr::asset::LoadCompletion> batch = worker.takeCompleted(count - out.size());
      out.insert(out.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
      if (out.size() < count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    return out;
  }

} // namespace yr_test