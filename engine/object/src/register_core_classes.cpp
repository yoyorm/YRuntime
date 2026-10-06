#include <yr/object/register_core_classes.h>

#include <yr/core/string_interner.h>
#include <yr/object/class_db.h>
#include <yr/object/class_info.h>

namespace yr::obj {

  namespace {

    // 核心类 Object 的 ClassInfo。函数内 static：首次调用时创建一次，避开 SIOF。
    // Object 无属性、无工厂（不可直接实例化）。
    const ClassInfo& objectClassInfo() {
      static const ClassInfo info(yr::core::globalStringInterner().intern("Object"), nullptr, {});
      return info;
    }

  } // namespace

  void register_core_classes() {
    globalClassDB().registerClass(objectClassInfo());
  }

} // namespace yr::obj