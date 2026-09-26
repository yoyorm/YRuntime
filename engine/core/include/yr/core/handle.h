#pragma once
#include <cstdint>
#include <functional>

namespace yr::core {

  template <typename T> class SlotMap;

  struct HandleBits {
    static constexpr uint32_t kIndexBits = 24;
    static constexpr uint32_t kGenerationBits = 40;
    static constexpr uint64_t kIndexMask = (uint64_t(1) << kIndexBits) - 1;
  };

  template <typename T> class Handle {
  public:
    constexpr Handle() noexcept = default; // == invalid
    [[nodiscard]] constexpr bool valid() const noexcept { return raw_ != 0; }
    [[nodiscard]] constexpr uint32_t index() const noexcept {
      return static_cast<uint32_t>(raw_ & HandleBits::kIndexMask);
    }
    [[nodiscard]] constexpr uint64_t generation() const noexcept { return raw_ >> HandleBits::kIndexBits; }
    [[nodiscard]] constexpr uint64_t raw() const noexcept { return raw_; }
    [[nodiscard]] static constexpr Handle invalid() noexcept { return Handle{}; }

    // 从裸值还原
    [[nodiscard]] static constexpr Handle fromRaw(uint64_t raw) noexcept {
      Handle h;
      h.raw_ = raw;
      return h;
    }

    friend constexpr bool operator==(Handle lhs, Handle rhs) noexcept { return lhs.raw_ == rhs.raw_; }
    friend constexpr bool operator!=(Handle lhs, Handle rhs) noexcept { return !(lhs == rhs); }

  private:
    friend class SlotMap<T>;
    static constexpr Handle make(uint32_t index, uint64_t generation) noexcept {
      Handle h;
      h.raw_ = ((generation << HandleBits::kIndexBits) & ~HandleBits::kIndexMask) |
               (static_cast<uint64_t>(index) & HandleBits::kIndexMask);
      return h;
    }

    uint64_t raw_ = 0; // 0 = invalid
  };
} // namespace yr::core

namespace std {
  template <typename T> struct hash<yr::core::Handle<T>> {
    std::size_t operator()(const yr::core::Handle<T>& h) const noexcept { return std::hash<uint64_t>{}(h.raw()); }
  };
} // namespace std