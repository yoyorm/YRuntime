#include <yr/object/object.h>

#include <yr/core/assert.h>
#include <yr/object/object_db.h>

namespace yr::obj {

  Object::Object() : id_(globalObjectDB().registerObject(*this)) {}

  Object::~Object() {
    const bool unregistered = globalObjectDB().unregisterObject(id_);
    YR_VERIFY(unregistered);
  }

} // namespace yr::obj
