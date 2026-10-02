#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

#include <yr/core/assert.h>
#include <yr/core/object_id.h>
#include <yr/core/string_id.h>

namespace yr::obj::detail {

  // Variant 支持的类型集合。
  template <typename T>
  inline constexpr bool kSupportedVariantType =
      std::is_same_v<T, bool> || std::is_same_v<T, int64_t> || std::is_same_v<T, double> ||
      std::is_same_v<T, yr::core::StringId> || std::is_same_v<T, yr::core::ObjectID> || std::is_same_v<T, std::string>;

} // namespace yr::obj::detail

namespace yr::obj {
  class Variant {
  public:
    enum class Type : uint8_t { kNull, kBool, kInt, kFloat, kStringId, kObjectID, kString };

    Variant() noexcept = default;

    ~Variant();
    Variant(const Variant& o);
    Variant(Variant&& o) noexcept;
    Variant& operator=(const Variant& o);
    Variant& operator=(Variant&& o) noexcept;

    template <typename T>
      requires detail::kSupportedVariantType<T>
    Variant(T value) noexcept {
      assign(value);
    }

    [[nodiscard]] constexpr Type type() const noexcept { return type_; }
    [[nodiscard]] constexpr bool isNil() const noexcept { return type_ == Type::kNull; }

    template <typename T>
      requires detail::kSupportedVariantType<T>
    [[nodiscard]] constexpr bool is() const noexcept {
      return type_ == typeOf<T>();
    }

    template <typename T>
      requires detail::kSupportedVariantType<T>
    [[nodiscard]] constexpr T as() const noexcept {
      if (type_ != typeOf<T>()) {
        YR_ASSERT_MSG(false, "Variant::as: type mismatch");
        return T{};
      }
      if constexpr (std::is_same_v<T, bool>) {
        return data_.b;
      } else if constexpr (std::is_same_v<T, int64_t>) {
        return data_.i;
      } else if constexpr (std::is_same_v<T, double>) {
        return data_.f;
      } else if constexpr (std::is_same_v<T, yr::core::StringId>) {
        return data_.sid;
      } else if constexpr (std::is_same_v<T, std::string>) {
        return *data_.str;
      } else {
        return data_.oid;
      }
    }

    // 类型不符返回 nullopt
    template <typename T>
      requires detail::kSupportedVariantType<T>
    [[nodiscard]] constexpr std::optional<T> tryAs() const noexcept {
      if (type_ != typeOf<T>()) {
        return std::nullopt;
      }
      return as<T>();
    }

    template <typename T>
      requires detail::kSupportedVariantType<T>
    void set(T value) noexcept {
      assign(value);
    }

    // 回到 null；会先释放当前持有的堆对象。
    void reset() noexcept;

    [[nodiscard]] bool operator==(const Variant& other) const noexcept;
    [[nodiscard]] bool operator!=(const Variant& other) const noexcept { return !(*this == other); }

  private:
    // 写入值与 tag。构造函数和 set() 共用。先释放旧的活跃成员，避免泄漏与 UB。
    template <typename T>
      requires detail::kSupportedVariantType<T>
    void assign(T value) noexcept {
      destroyActive();
      type_ = typeOf<T>();
      if constexpr (std::is_same_v<T, bool>) {
        data_.b = value;
      } else if constexpr (std::is_same_v<T, int64_t>) {
        data_.i = value;
      } else if constexpr (std::is_same_v<T, double>) {
        data_.f = value;
      } else if constexpr (std::is_same_v<T, yr::core::StringId>) {
        data_.sid = value;
      } else if constexpr (std::is_same_v<T, std::string>) {
        std::construct_at(&data_.str, std::make_unique<std::string>(std::move(value)));
      } else {
        data_.oid = value;
      }
    }

    template <typename T> static constexpr Type typeOf() noexcept {
      static_assert(detail::kSupportedVariantType<T>, "Variant: unsupported type");
      if constexpr (std::is_same_v<T, bool>) {
        return Type::kBool;
      } else if constexpr (std::is_same_v<T, int64_t>) {
        return Type::kInt;
      } else if constexpr (std::is_same_v<T, double>) {
        return Type::kFloat;
      } else if constexpr (std::is_same_v<T, yr::core::StringId>) {
        return Type::kStringId;
      } else if constexpr (std::is_same_v<T, std::string>) {
        return Type::kString;
      } else {
        return Type::kObjectID;
      }
    }

    // 结束当前 type_ 对应的堆成员生命周期,不修改 type_。
    void destroyActive() noexcept {
      switch (type_) {
      case Type::kString:
        std::destroy_at(&data_.str);
        return;
      default:
        return;
      }
    }

    // 从 o 深拷贝活跃成员；调用前 data_ 没有活跃堆成员。
    void copyFrom(const Variant& o);
    // 从 o 移动活跃成员
    void moveFrom(Variant& o);

    Type type_ = Type::kNull;
    union Storage {
      constexpr Storage() noexcept : i(0) {}
      // 成员生命周期由 Variant 的 destroyActive() 手动管理。
      ~Storage() noexcept {}
      int64_t i;
      bool b;
      double f;
      yr::core::StringId sid;
      yr::core::ObjectID oid;
      std::unique_ptr<std::string> str;
    };
    Storage data_;
  };

} // namespace yr::obj