#include <yr/object/class_info.h>

namespace yr::obj {

  // 查找本类优先，未命中则沿 parent_ 递归。
  const PropertyInfo* ClassInfo::findProperty(yr::core::StringId name) const noexcept {
    for (const PropertyInfo& p : properties_) {
      if (p.name == name)
        return &p;
    }
    return parent_ ? parent_->findProperty(name) : nullptr;
  }

  // 从 this 沿 parent_ 递归，命中 base 返回 true。
  bool ClassInfo::isA(const ClassInfo* base) const noexcept {
    for (const ClassInfo* c = this; c != nullptr; c = c->parent_) {
      if (c == base)
        return true;
    }
    return false;
  }

  bool ClassInfo::get_property(const Object* obj, yr::core::StringId name, Variant& out) const {
    const PropertyInfo* p = findProperty(name);
    if (p == nullptr || p->getter == nullptr) {
      return false;
    }
    out = p->getter(obj);
    return true;
  }

  bool ClassInfo::set_property(Object* obj, yr::core::StringId name, const Variant& value) const {
    const PropertyInfo* p = findProperty(name);
    if (p == nullptr || p->setter == nullptr) {
      return false;
    }
    if ((p->flags & kReadOnly) != 0) {
      return false;
    }
    return p->setter(obj, value); // setter 内部还要再查一次 Variant 类型
  }

} // namespace yr::obj