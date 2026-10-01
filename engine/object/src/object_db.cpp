#include <yr/object/object_db.h>

#include <utility>

#include <yr/core/assert.h>
#include <yr/object/object.h>

namespace yr::obj {

  yr::core::ObjectID ObjectDB::registerObject(Object& object) {
    std::uint32_t index = 0;

    if (freelist_.empty()) {
      YR_ASSERT(slots_.size() <= yr::core::ObjectIDBits::kIndexMask);
      index = static_cast<std::uint32_t>(slots_.size());
      slots_.push_back({&object, 1});
    } else {
      index = freelist_.back();
      freelist_.pop_back();

      ObjectSlot& slot = slots_[index];
      YR_ASSERT(slot.object == nullptr);
      slot.object = &object;
    }

    ++aliveCount_;
    return yr::core::ObjectID::make(index, slots_[index].generation);
  }

  bool ObjectDB::unregisterObject(yr::core::ObjectID id) noexcept {
    if (!contains(id)) {
      return false;
    }

    ObjectSlot& slot = slots_[id.index()];
    slot.object = nullptr;
    ++slot.generation;
    freelist_.push_back(id.index());
    --aliveCount_;
    return true;
  }

  Object* ObjectDB::get(yr::core::ObjectID id) noexcept {
    return const_cast<Object*>(std::as_const(*this).get(id));
  }

  const Object* ObjectDB::get(yr::core::ObjectID id) const noexcept {
    if (!contains(id)) {
      return nullptr;
    }
    return slots_[id.index()].object;
  }

  bool ObjectDB::contains(yr::core::ObjectID id) const noexcept {
    if (!id.valid() || id.index() >= slots_.size()) {
      return false;
    }

    const ObjectSlot& slot = slots_[id.index()];
    return slot.object != nullptr && slot.generation == id.generation();
  }

  std::size_t ObjectDB::aliveCount() const noexcept {
    return aliveCount_;
  }

  ObjectDB& globalObjectDB() noexcept {
    static ObjectDB database;
    return database;
  }

} // namespace yr::obj
