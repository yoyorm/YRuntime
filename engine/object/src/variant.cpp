#include <yr/object/variant.h>

#include <utility>

namespace yr::obj {

  Variant::~Variant() {
    destroyActive();
  }

  Variant::Variant(const Variant& o) : type_(o.type_) {
    copyFrom(o);
  }

  Variant::Variant(Variant&& o) noexcept : type_(o.type_) {
    moveFrom(o);
    o.destroyActive(); // 结束源中被掏空的成员生命周期（moved-from unique_ptr 为 null）
    o.type_ = Type::kNull;
  }

  Variant& Variant::operator=(const Variant& o) {
    if (this == &o) {
      return *this;
    }
    destroyActive();
    type_ = o.type_;
    copyFrom(o);
    return *this;
  }

  Variant& Variant::operator=(Variant&& o) noexcept {
    if (this == &o) {
      return *this;
    }
    destroyActive();
    type_ = o.type_;
    moveFrom(o);
    o.destroyActive();
    o.type_ = Type::kNull;
    return *this;
  }

  void Variant::reset() noexcept {
    destroyActive();
    type_ = Type::kNull;
  }

  bool Variant::operator==(const Variant& other) const noexcept {
    if (type_ != other.type_) {
      return false;
    }
    switch (type_) {
    case Type::kNull:
      return true;
    case Type::kBool:
      return data_.b == other.data_.b;
    case Type::kInt:
      return data_.i == other.data_.i;
    case Type::kFloat:
      return data_.f == other.data_.f;
    case Type::kStringId:
      return data_.sid == other.data_.sid;
    case Type::kObjectID:
      return data_.oid == other.data_.oid;
    case Type::kString:
      return *data_.str == *other.data_.str; // 解引用比较内容
    case Type::kArray:
      return *data_.arr == *other.data_.arr; // vector == 逐元素递归调用 Variant::operator==
    case Type::kDict:
      // vector<pair> 逐对比较：先 StringId，再 Variant；保序 → 顺序不同即不等
      return *data_.dict == *other.data_.dict;
    case Type::kVec3:
      return data_.v3 == other.data_.v3;
    }
    return false;
  }

  void Variant::copyFrom(const Variant& o) {
    switch (o.type_) {
    case Type::kNull:
      return;
    case Type::kBool:
      std::construct_at(&data_.b, o.data_.b);
      return;
    case Type::kInt:
      std::construct_at(&data_.i, o.data_.i);
      return;
    case Type::kFloat:
      std::construct_at(&data_.f, o.data_.f);
      return;
    case Type::kStringId:
      std::construct_at(&data_.sid, o.data_.sid);
      return;
    case Type::kObjectID:
      std::construct_at(&data_.oid, o.data_.oid);
      return;
    case Type::kString:
      std::construct_at(&data_.str, std::make_unique<std::string>(*o.data_.str)); // 深拷贝
      return;
    case Type::kArray:
      std::construct_at(&data_.arr, std::make_unique<Array>(*o.data_.arr)); // 深拷贝（逐元素递归）
      return;
    case Type::kDict:
      std::construct_at(&data_.dict, std::make_unique<Dict>(*o.data_.dict)); // 深拷贝（逐对递归）
      return;
    case Type::kVec3:
      std::construct_at(&data_.v3, o.data_.v3);
      return;
    }
  }

  void Variant::moveFrom(Variant& o) {
    switch (o.type_) {
    case Type::kNull:
      return;
    case Type::kBool:
      std::construct_at(&data_.b, o.data_.b);
      return;
    case Type::kInt:
      std::construct_at(&data_.i, o.data_.i);
      return;
    case Type::kFloat:
      std::construct_at(&data_.f, o.data_.f);
      return;
    case Type::kStringId:
      std::construct_at(&data_.sid, o.data_.sid);
      return;
    case Type::kObjectID:
      std::construct_at(&data_.oid, o.data_.oid);
      return;
    case Type::kString:
      std::construct_at(&data_.str, std::move(o.data_.str)); // 偷指针
      return;
    case Type::kArray:
      std::construct_at(&data_.arr, std::move(o.data_.arr)); // 偷指针
      return;
    case Type::kDict:
      std::construct_at(&data_.dict, std::move(o.data_.dict)); // 偷指针
      return;
    case Type::kVec3:
      std::construct_at(&data_.v3, o.data_.v3);
      return;
    }
  }

} // namespace yr::obj