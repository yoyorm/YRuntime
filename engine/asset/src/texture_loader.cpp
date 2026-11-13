#include <yr/asset/texture_loader.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include <stb_image.h>

namespace yr::asset {

  void downscaleRgba8(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t maxDimension,
                      DecodedTexture& out) {
    out.width = 0;
    out.height = 0;
    out.rgba8.clear();
    if (pixels == nullptr || width == 0 || height == 0) {
      return;
    }

    const std::uint32_t sourceMax = std::max(width, height);
    const std::uint64_t sourceBytes = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
    if (sourceBytes > std::numeric_limits<std::size_t>::max()) {
      return;
    }

    if (maxDimension == 0 || sourceMax <= maxDimension) {
      out.width = width;
      out.height = height;
      out.rgba8.assign(pixels, pixels + static_cast<std::size_t>(sourceBytes));
      return;
    }

    const double scale = static_cast<double>(maxDimension) / static_cast<double>(sourceMax);
    const auto clampDimension = [maxDimension](double value) {
      const auto rounded = static_cast<std::uint32_t>(std::max(1.0, std::round(value)));
      return std::min(rounded, maxDimension);
    };
    const std::uint32_t targetWidth = clampDimension(static_cast<double>(width) * scale);
    const std::uint32_t targetHeight = clampDimension(static_cast<double>(height) * scale);

    const std::uint64_t targetBytes =
        static_cast<std::uint64_t>(targetWidth) * static_cast<std::uint64_t>(targetHeight) * 4ULL;
    if (targetBytes > std::numeric_limits<std::size_t>::max()) {
      return;
    }

    out.width = targetWidth;
    out.height = targetHeight;
    out.rgba8.resize(static_cast<std::size_t>(targetBytes));

    for (std::uint32_t targetY = 0; targetY < targetHeight; ++targetY) {
      const std::uint64_t y0 = static_cast<std::uint64_t>(targetY) * height / targetHeight;
      std::uint64_t y1 = (static_cast<std::uint64_t>(targetY) + 1) * height / targetHeight;
      y1 = std::min<std::uint64_t>(std::max(y1, y0 + 1), height);

      for (std::uint32_t targetX = 0; targetX < targetWidth; ++targetX) {
        const std::uint64_t x0 = static_cast<std::uint64_t>(targetX) * width / targetWidth;
        std::uint64_t x1 = (static_cast<std::uint64_t>(targetX) + 1) * width / targetWidth;
        x1 = std::min<std::uint64_t>(std::max(x1, x0 + 1), width);

        std::uint64_t sums[4] = {0, 0, 0, 0};
        std::uint64_t count = 0;
        for (std::uint64_t y = y0; y < y1; ++y) {
          const std::uint8_t* const row = pixels + y * width * 4;
          for (std::uint64_t x = x0; x < x1; ++x) {
            const std::uint8_t* const texel = row + x * 4;
            sums[0] += texel[0];
            sums[1] += texel[1];
            sums[2] += texel[2];
            sums[3] += texel[3];
            ++count;
          }
        }

        std::uint8_t* const target = out.rgba8.data() + (static_cast<std::size_t>(targetY) * targetWidth + targetX) * 4;
        for (std::uint32_t channel = 0; channel < 4; ++channel) {
          target[channel] = static_cast<std::uint8_t>((sums[channel] + count / 2) / count);
        }
      }
    }
  }

  bool decodeRgba8(const std::uint8_t* data, std::size_t size, std::uint32_t maxDimension, DecodedTexture& out,
                   std::string& error) {
    out.width = 0;
    out.height = 0;
    out.rgba8.clear();
    error.clear();

    if (data == nullptr) {
      error = "纹理解码失败：输入数据为空";
      return false;
    }
    if (size == 0) {
      error = "纹理解码失败：输入数据为零字节";
      return false;
    }
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      error = "纹理解码失败：输入数据过大";
      return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* const pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (pixels == nullptr) {
      const char* const reason = stbi_failure_reason();
      error = "纹理解码失败：" + std::string(reason != nullptr ? reason : "stbi_load_from_memory 失败");
      return false;
    }
    if (width <= 0 || height <= 0) {
      stbi_image_free(pixels);
      error = "纹理解码失败：图片尺寸无效";
      return false;
    }

    const std::uint64_t pixelBytes = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
    if (pixelBytes > std::numeric_limits<std::size_t>::max()) {
      stbi_image_free(pixels);
      error = "纹理解码失败：图片过大";
      return false;
    }

    downscaleRgba8(pixels, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), maxDimension, out);
    stbi_image_free(pixels);

    const std::uint64_t expectedBytes =
        static_cast<std::uint64_t>(out.width) * static_cast<std::uint64_t>(out.height) * 4ULL;
    if (out.width == 0 || out.height == 0 || out.rgba8.size() != expectedBytes) {
      out.width = 0;
      out.height = 0;
      out.rgba8.clear();
      error = "纹理解码失败：RGBA 数据大小不匹配";
      return false;
    }
    return true;
  }

} // namespace yr::asset