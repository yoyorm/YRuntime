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
    static constexpr std::uint64_t kGenerationMask = (std::uint64_t{1} << kGenerationBits) - 1;

    // 把 index / generation 规范编码进 64-bit raw。generation 先掩码到 40 位，
    // 这样 ObjectID::make() 与 ObjectDB 存储的 generation 永远落在同一取值域。
    [[nodiscard]] static constexpr std::uint64_t encode(std::uint32_t index, std::uint64_t generation) noexcept {
      return ((generation & kGenerationMask) << kIndexBits) | (static_cast<std::uint64_t>(index) & kIndexMask);
    }

    // generation 前进一格并规范化到 40 位；回绕到 0 时跳到 1（0 保留给 invalid ID）。
    [[nodiscard]] static constexpr std::uint64_t nextGeneration(std::uint64_t generation) noexcept {
      const std::uint64_t next = (generation + 1) & kGenerationMask;
      return next == 0 ? 1 : next;
    }
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
      id.raw_ = ObjectIDBits::encode(index, generation);
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
