#pragma once

#include <memory>
#include <span>

#include <yr/core/string_id.h>
#include <yr/object/property_info.h>

namespace yr::obj {

  class Object;

  // 单个类的静态元数据。
  // properties_ 指向静态 PropertyInfo 数组，该数组必须比 ClassInfo 活得更久；工厂返回
  // unique_ptr，创建完成即确定唯一所有者。
  class ClassInfo {
  public:
    // 按类创建实例的工厂。调用方接管返回值；需要 Ref/裸指针时显式 release。
    using Factory = std::unique_ptr<Object> (*)();

    constexpr ClassInfo(yr::core::StringId name, const ClassInfo* parent, std::span<const PropertyInfo> properties,
                        Factory factoryFn = nullptr) noexcept
        : name_(name), parent_(parent), properties_(properties), factory_(factoryFn) {}

    [[nodiscard]] constexpr yr::core::StringId name() const noexcept { return name_; }
    [[nodiscard]] constexpr const ClassInfo* parent() const noexcept { return parent_; }

    // 仅本类的属性表；不含继承属性
    [[nodiscard]] constexpr std::span<const PropertyInfo> properties() const noexcept { return properties_; }

    // 工厂；nullptr = 抽象类
    [[nodiscard]] constexpr Factory factory() const noexcept { return factory_; }

    // 沿继承链查找属性,未命中则递归 parent_
    [[nodiscard]] const PropertyInfo* findProperty(yr::core::StringId name) const noexcept;

    // this 是否就是 base，或派生自 base（沿 parent_ 向上逐级比较指针）。
    [[nodiscard]] bool isA(const ClassInfo* base) const noexcept;

    bool get_property(const Object* obj, yr::core::StringId name, Variant& out) const;
    bool set_property(Object* obj, yr::core::StringId name, const Variant& value) const;

  private:
    yr::core::StringId name_;
    const ClassInfo* parent_ = nullptr;
    std::span<const PropertyInfo> properties_{};
    Factory factory_ = nullptr;
  };

} // namespace yr::obj