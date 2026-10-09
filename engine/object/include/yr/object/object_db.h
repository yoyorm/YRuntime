#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <yr/core/object_id.h>

namespace yr::obj {

  class Object;

  // 全局、非拥有的 ObjectID -> Object* 索引。
  // 只按 ID 随机访问，因此保留空 slot 并用 freelist 复用，不维护稠密遍历顺序。
  // 当前仅允许主线程访问；调用方必须在 Object 内存失效前注销。
  class ObjectDB {
  public:
    ~ObjectDB() = default;

    ObjectDB(const ObjectDB&) = delete;
    ObjectDB& operator=(const ObjectDB&) = delete;
    ObjectDB(ObjectDB&&) = delete;
    ObjectDB& operator=(ObjectDB&&) = delete;

    [[nodiscard]] Object* get(yr::core::ObjectID id) noexcept;
    [[nodiscard]] const Object* get(yr::core::ObjectID id) const noexcept;
    [[nodiscard]] bool contains(yr::core::ObjectID id) const noexcept;
    [[nodiscard]] std::size_t aliveCount() const noexcept;

  private:
    struct ObjectSlot {
      Object* object = nullptr;
      // 规范化到 ObjectIDBits::kGenerationBits 位；0 保留给 invalid ID，回绕时跳过 0。
      std::uint64_t generation = 1;
    };

    ObjectDB() = default;

    friend ObjectDB& globalObjectDB() noexcept;
    friend class Object;

    [[nodiscard]] yr::core::ObjectID registerObject(Object& object);
    bool unregisterObject(yr::core::ObjectID id) noexcept;

    std::vector<ObjectSlot> slots_;
    std::vector<std::uint32_t> freelist_;
    std::size_t aliveCount_ = 0;
  };

  [[nodiscard]] ObjectDB& globalObjectDB() noexcept;

} // namespace yr::obj