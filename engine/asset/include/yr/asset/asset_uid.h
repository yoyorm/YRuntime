#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace yr::asset {

  struct AssetPath {
    std::string canonical;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
  };

  // 只接受 res:// 虚拟路径；成功后 canonical 是原路径（大小写敏感、分隔符固定 '/'，
  // 不做其他归一化）。空路径、空/点/点点 segment、尾 '/'、反斜杠、引号、控制字符 → error。
  [[nodiscard]] AssetPath canonicalizeAssetPath(std::string_view path);

  struct AssetUID {
    std::uint64_t value = 0; // 0 = invalid

    [[nodiscard]] static AssetUID fromPath(std::string_view path);
    [[nodiscard]] bool valid() const noexcept { return value != 0; }
    [[nodiscard]] std::string toString() const; // 16 位小写 hex
  };

  [[nodiscard]] constexpr bool operator==(AssetUID lhs, AssetUID rhs) noexcept {
    return lhs.value == rhs.value;
  }

  [[nodiscard]] constexpr bool operator!=(AssetUID lhs, AssetUID rhs) noexcept {
    return !(lhs == rhs);
  }

} // namespace yr::asset

namespace std {
  template <> struct hash<yr::asset::AssetUID> {
    [[nodiscard]] std::size_t operator()(const yr::asset::AssetUID& uid) const noexcept {
      return std::hash<std::uint64_t>{}(uid.value);
    }
  };
} // namespace std