#include <yr/object/register_core_classes.h>

#include <yr/core/string_interner.h>
#include <yr/object/class_db.h>
#include <yr/object/class_info.h>

namespace yr::obj {

  // 核心类 Object 的 ClassInfo
  const ClassInfo& objectClassInfo() noexcept {
    static const ClassInfo info(yr::core::globalStringInterner().intern("Object"), nullptr, {});
    return info;
  }

  void register_core_classes() {
    static bool registered = false;
    if (registered) {
      return;
    }
    globalClassDB().registerClass(objectClassInfo());
    registered = true;
  }

} // namespace yr::obj