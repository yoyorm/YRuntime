#include <yr/object/ref_counted.h>

#include <yr/core/assert.h>

namespace yr::obj {

  RefCounted::~RefCounted() {
    YR_ASSERT_MSG(refcount() == 0, "RefCounted destroyed with live references");
  }

  void RefCounted::ref() const noexcept {
    refcount_.fetch_add(1, std::memory_order_relaxed);
  }

  bool RefCounted::unref() const noexcept {
    const std::uint32_t previous = refcount_.fetch_sub(1, std::memory_order_acq_rel);
    YR_ASSERT_MSG(previous > 0, "RefCounted::unref called with refcount 0");
    if (previous == 0) {
      refcount_.store(0, std::memory_order_relaxed);
      return false;
    }
    return previous == 1;
  }

  std::uint32_t RefCounted::refcount() const noexcept {
    return refcount_.load(std::memory_order_relaxed);
  }

} // namespace yr::obj