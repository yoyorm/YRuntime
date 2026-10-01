#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace yr::obj {

  class ObjectDB;

} // namespace yr::obj

namespace yr::core {

  struct ObjectIDBits {
    static constexpr std::uint32_t kIndexBits = 24;
    static constexpr std::uint32_t kGenerationBits = 40;
    static constexpr std::uint64_t kIndexMask = (std::uint64_t{1} << kIndexBits) - 1;
  };

  // 全局对象身份：只描述对象是谁，不拥有对象，也不负责对象生命周期。
  // 有效 ID 由 ObjectDB 创建；对象销毁后，ObjectDB 使对应 generation 失效。
  class ObjectID {
  public:
    constexpr ObjectID() noexcept = default;

    [[nodiscard]] constexpr bool valid() const noexcept { return raw_ != 0; }

    [[nodiscard]] constexpr std::uint32_t index() const noexcept {
      return static_cast<std::uint32_t>(raw_ & ObjectIDBits::kIndexMask);
    }

    [[nodiscard]] constexpr std::uint64_t generation() const noexcept { return raw_ >> ObjectIDBits::kIndexBits; }

    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return raw_; }

    [[nodiscard]] static constexpr ObjectID invalid() noexcept { return {}; }

    friend constexpr bool operator==(ObjectID, ObjectID) noexcept = default;

  private:
    static constexpr ObjectID make(std::uint32_t index, std::uint64_t generation) noexcept {
      ObjectID id;
      id.raw_ = ((generation << ObjectIDBits::kIndexBits) & ~ObjectIDBits::kIndexMask) |
                (static_cast<std::uint64_t>(index) & ObjectIDBits::kIndexMask);
      return id;
    }

    friend class yr::obj::ObjectDB;

    std::uint64_t raw_ = 0;
  };

} // namespace yr::core

namespace std {

  template <> struct hash<yr::core::ObjectID> {
    [[nodiscard]] std::size_t operator()(yr::core::ObjectID id) const noexcept {
      return std::hash<std::uint64_t>{}(id.raw());
    }
  };

} // namespace std
