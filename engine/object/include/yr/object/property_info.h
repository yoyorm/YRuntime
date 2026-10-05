#pragma once

#include <cstdint>

#include <yr/core/string_id.h>
#include <yr/object/variant.h>

namespace yr::obj {

  class Object;

  // 属性标志位
  enum PropertyFlags : std::uint32_t {
    kNone = 0,
    kEditable = 1U << 0,   // 编辑器/检视器可修改
    kSerialized = 1U << 1, // 参与存档序列化
    kReadOnly = 1U << 2,   // 只读：set_property 必须拒绝
  };

  using PropertyGetter = Variant (*)(const Object*);
  using PropertySetter = bool (*)(Object*, const Variant&);

  // 单个属性的元数据, 每个类一份。
  struct PropertyInfo {
    yr::core::StringId name;                   // 属性名（驻留字符串，比较 O(1)）
    Variant::Type type = Variant::Type::kNull; // 该属性的 Variant 类型，set 前用于校验
    std::uint32_t flags = kNone;               // PropertyFlags 位或
    yr::core::StringId class_hint{};           // 对象类型属性的目标类名；其余属性留空
    PropertyGetter getter = nullptr;           // nullptr = 只写属性
    PropertySetter setter = nullptr;           // nullptr = 只读属性
  };

} // namespace yr::obj