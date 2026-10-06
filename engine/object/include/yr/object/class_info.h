#pragma once

#include <span>

#include <yr/core/string_id.h>
#include <yr/object/property_info.h>

namespace yr::obj {

  class Object;

  // 单个类的元数据。只保存静态信息。
  //
  // 所有权：name_ 是驻留 yr::core::StringId；properties_ 是指向「静态 PropertyInfo 数组」的 span，
  // 该数组必须比 ClassInfo 活得更久（手工注册时用函数内 static 数组，避开 SIOF）。
  // factory 由注册代码提供；抽象类为 nullptr。它返回裸指针，所有权/回收交给 Ref（M2 后续）。
  class ClassInfo {
  public:
    // 按类创建实例的工厂。返回裸 Object*（check_deps 禁裸 new，注册方用 make_unique/make_ref 实现）。
    using Factory = Object* (*)();

    constexpr ClassInfo(yr::core::StringId name, const ClassInfo* parent, std::span<const PropertyInfo> properties,
                        Factory factoryFn = nullptr) noexcept
        : name_(name), parent_(parent), properties_(properties), factory_(factoryFn) {}

    [[nodiscard]] constexpr yr::core::StringId name() const noexcept { return name_; }
    [[nodiscard]] constexpr const ClassInfo* parent() const noexcept { return parent_; }

    // 仅本类的属性表；不含继承来的属性。
    [[nodiscard]] constexpr std::span<const PropertyInfo> properties() const noexcept { return properties_; }

    // 工厂；nullptr = 抽象类，不可实例化。
    [[nodiscard]] constexpr Factory factory() const noexcept { return factory_; }

    // 沿继承链查找属性：本类优先，未命中则递归 parent_。找不到返回 nullptr。
    // 派生类可以定义与基类同名的属性来「遮蔽」基类属性。
    [[nodiscard]] const PropertyInfo* findProperty(yr::core::StringId name) const noexcept;

    // this 是否就是 base，或派生自 base（沿 parent_ 向上逐级比较指针）。
    [[nodiscard]] bool isA(const ClassInfo* base) const noexcept;

    // 按名字读属性到 out, 失败返回 false
    bool get_property(const Object* obj, yr::core::StringId name, Variant& out) const;

    // 按名字写属性, 失败返回 false
    bool set_property(Object* obj, yr::core::StringId name, const Variant& value) const;

  private:
    yr::core::StringId name_;
    const ClassInfo* parent_ = nullptr;
    std::span<const PropertyInfo> properties_{};
    Factory factory_ = nullptr;
  };

} // namespace yr::obj