#pragma once

#include <cstdint>

namespace yr::render {

  // 强类型资源句柄：默认无效(0)，跨边界只作不透明 key。正式 AssetUID 到 M9 替换。
  struct AssetHandle {
    std::uint64_t value = 0;
    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const AssetHandle&) const noexcept = default;
  };

} // namespace yr::render