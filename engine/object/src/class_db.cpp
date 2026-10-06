#include <yr/object/class_db.h>

#include <yr/core/assert.h>
#include <yr/object/object.h>

namespace yr::obj {

  void ClassDB::registerClass(const ClassInfo& info) {
    YR_ASSERT_MSG(!frozen_, "ClassDB::registerClass: registry is frozen");
    YR_ASSERT_MSG(classes_.find(info.name()) == classes_.end(), "ClassDB::registerClass: duplicate class name");
    classes_.emplace(info.name(), &info);
  }

  const ClassInfo* ClassDB::getClass(yr::core::StringId name) const noexcept {
    const auto it = classes_.find(name);
    return it == classes_.end() ? nullptr : it->second;
  }

  Object* ClassDB::instantiate(yr::core::StringId name) const {
    const ClassInfo* info = getClass(name);
    if (info == nullptr || info->factory() == nullptr) {
      return nullptr; // 未注册 / 抽象类：返回 nullptr，由调用方决策（将来记 WARN 日志）
    }
    return info->factory()();
  }

  std::vector<yr::core::StringId> ClassDB::allClasses() const {
    std::vector<yr::core::StringId> names;
    names.reserve(classes_.size());
    for (const auto& entry : classes_) {
      names.push_back(entry.first);
    }
    return names;
  }

  std::vector<yr::core::StringId> ClassDB::inheritorsOf(yr::core::StringId name) const {
    std::vector<yr::core::StringId> result;
    const ClassInfo* base = getClass(name);
    if (base == nullptr) {
      return result;
    }
    for (const auto& entry : classes_) {
      if (entry.second != base && entry.second->isA(base)) {
        result.push_back(entry.first);
      }
    }
    return result;
  }

  ClassDB& globalClassDB() noexcept {
    static ClassDB database;
    return database;
  }

} // namespace yr::obj