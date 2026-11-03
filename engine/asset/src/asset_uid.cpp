#include <yr/asset/asset_uid.h>

#include <cstdio>

namespace yr::asset {

  namespace {

    constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

    [[nodiscard]] bool isInvalidCharacter(char c) noexcept {
      const auto byte = static_cast<unsigned char>(c);
      return c == '\\' || c == '"' || byte < 0x20U || byte == 0x7FU;
    }

  } // namespace

  AssetPath canonicalizeAssetPath(std::string_view path) {
    constexpr std::string_view kPrefix = "res://";

    AssetPath result;
    if (!path.starts_with(kPrefix)) {
      result.error = "路径必须以 res:// 开头";
      return result;
    }

    const std::string_view relative = path.substr(kPrefix.size());
    if (relative.empty()) {
      result.error = "路径不能为空";
      return result;
    }

    for (const char c : path) {
      if (isInvalidCharacter(c)) {
        result.error = "路径包含反斜杠、引号或控制字符";
        return result;
      }
    }

    std::size_t start = 0;
    while (start <= relative.size()) {
      const std::size_t slash = relative.find('/', start);
      const std::size_t end = slash == std::string_view::npos ? relative.size() : slash;
      const std::string_view segment = relative.substr(start, end - start);
      if (segment.empty()) {
        result.error = "路径不能包含空 segment 或以 '/' 结尾";
        return result;
      }
      if (segment == "." || segment == "..") {
        result.error = "路径不能包含 '.' 或 '..' segment";
        return result;
      }
      if (slash == std::string_view::npos) {
        break;
      }
      start = slash + 1;
    }

    result.canonical = std::string(path);
    return result;
  }

  AssetUID AssetUID::fromPath(std::string_view path) {
    const AssetPath canonical = canonicalizeAssetPath(path);
    if (!canonical.ok()) {
      return AssetUID{};
    }

    std::uint64_t hash = kFnvOffset;
    for (const unsigned char byte : canonical.canonical) {
      hash ^= byte;
      hash *= kFnvPrime;
    }
    if (hash == 0) {
      hash = 1;
    }
    return AssetUID{hash};
  }

  std::string AssetUID::toString() const {
    char buffer[17] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buffer);
  }

} // namespace yr::asset