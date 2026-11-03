#include <yr/object/ref_counted.h>

#include <yr/core/assert.h>

namespace yr::obj {

  RefCounted::~RefCounted() {
    YR_ASSERT_MSG(refcount() == 0, "RefCounted destroyed with live references");
  }

  void RefCounted::ref() const noexcept {
    // relaxed：已经持有引用的一方再增加计数，不需要与其他线程的读写建立同步。
    refcount_.fetch_add(1, std::memory_order_relaxed);
  }

  bool RefCounted::unref() const noexcept {
    // acq_rel：release 保证之前的对象写入先于最终 delete 可见；acquire 保证最后一个
    // 释放者看到所有写入。单线程（M9）同样正确。
    const std::uint32_t previous = refcount_.fetch_sub(1, std::memory_order_acq_rel);
    YR_ASSERT_MSG(previous > 0, "RefCounted::unref called with refcount 0");
    if (previous == 0) {
      // 误用：debug 下断言已终止；release 下把计数恢复为 0，避免 uint32 回绕污染后续计数。
      refcount_.store(0, std::memory_order_relaxed);
      return false;
    }
    return previous == 1;
  }

  std::uint32_t RefCounted::refcount() const noexcept {
    // relaxed：诊断读取，不建立同步；需要同步的释放路径由 unref() 的 acq_rel 负责。
    return refcount_.load(std::memory_order_relaxed);
  }

} // namespace yr::obj