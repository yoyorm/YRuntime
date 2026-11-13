#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace yr::asset {

  struct DecodedTexture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8;
  };

  // maxDimension == 0 或 max(width,height) <= maxDimension 时原样拷贝。
  // 否则用 box filter 下采样，保持宽高比，目标宽高至少 1。
  void downscaleRgba8(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t maxDimension,
                      DecodedTexture& out);

  // stb 解码为 RGBA8，然后 downscaleRgba8；失败返回 false 并写 error。
  [[nodiscard]] bool decodeRgba8(const std::uint8_t* data, std::size_t size, std::uint32_t maxDimension,
                                 DecodedTexture& out, std::string& error);

} // namespace yr::asset
