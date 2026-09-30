#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace yr::core {

  class StringInterner;

  struct StringNode {
    explicit StringNode(std::string text) : value(std::move(text)) {}

    std::string value;
  };

  class StringId {
  public:
    constexpr StringId() noexcept = default;

    [[nodiscard]] constexpr bool valid() const noexcept { return node_ != nullptr; }

    [[nodiscard]] std::string_view view() const noexcept;

    [[nodiscard]] static constexpr StringId invalid() noexcept { return {}; }

    friend constexpr bool operator==(StringId, StringId) noexcept = default;

  private:
    explicit constexpr StringId(const StringNode* node) noexcept : node_(node) {}

    friend class StringInterner;
    friend struct std::hash<StringId>;

    const StringNode* node_ = nullptr;
  };
} // namespace yr::core

namespace std {

  template <> struct hash<yr::core::StringId> {
    [[nodiscard]] std::size_t operator()(yr::core::StringId id) const noexcept {
      return std::hash<const void*>{}(id.node_);
    }
  };

} // namespace std
