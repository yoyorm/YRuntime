#include <yr/object/ref_counted.h>

namespace yr::obj {

  void RefCounted::ref() const noexcept {
    // TODO(user): 递增 refcount_；选择 memory_order。
  }

  bool RefCounted::unref() const noexcept {
    // TODO(user): 递减 refcount_；归零返回 true。
    return false;
  }

  std::uint32_t RefCounted::refcount() const noexcept {
    // TODO(user): 返回当前计数。
    return 0;
  }

} // namespace yr::obj