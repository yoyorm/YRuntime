#pragma once

#include <unordered_map>
#include <vector>

#include <yr/core/string_id.h>
#include <yr/object/class_info.h>

namespace yr::obj {

  class Object;

  // 进程级类注册表：yr::core::StringId 类名 → const ClassInfo*（非拥有）。
  // 与 ObjectDB 同构：只做索引，不拥有被索引的静态元数据。
  // ClassInfo 由各类自己的注册函数创建（函数内 static）；本类只登记它们的地址。
  // 约定：注册只发生在启动期，之后 freeze()，此后 get/查询线程只读、无需加锁。
  class ClassDB {
  public:
    ClassDB() = default;
    ~ClassDB() = default;

    ClassDB(const ClassDB&) = delete;
    ClassDB& operator=(const ClassDB&) = delete;
    ClassDB(ClassDB&&) = delete;
    ClassDB& operator=(ClassDB&&) = delete;

    // 注册一个类。freeze 之后、或名字重复 → Debug 下 assert。
    void registerClass(const ClassInfo& info);

    // 冻结：此后禁止注册（表征"启动期注册已完成"）。
    void freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

    // 按类名查 ClassInfo；未注册返回 nullptr。
    [[nodiscard]] const ClassInfo* getClass(yr::core::StringId name) const noexcept;

    // 按类名创建实例；未注册或无工厂（抽象类）返回 nullptr。所有权交给调用方（未来是 Ref）。
    [[nodiscard]] Object* instantiate(yr::core::StringId name) const;

    // 所有已注册的类名。
    [[nodiscard]] std::vector<yr::core::StringId> allClasses() const;

    // 继承自 name 的所有类名（不含 name 自身）；name 未注册则返回空。
    [[nodiscard]] std::vector<yr::core::StringId> inheritorsOf(yr::core::StringId name) const;

  private:
    std::unordered_map<yr::core::StringId, const ClassInfo*> classes_;
    bool frozen_ = false;
  };

  // 进程级默认注册表。公共使用路径通过它共享同一个类空间。
  [[nodiscard]] ClassDB& globalClassDB() noexcept;

} // namespace yr::obj