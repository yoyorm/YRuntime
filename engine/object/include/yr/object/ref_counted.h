#pragma once

#include <atomic>
#include <cstdint>

#include <yr/object/object.h>

namespace yr::obj {

  // M9-0：引用计数基类。
  //
  // - 构造时 refcount 为 0；Ref<T> 持有时计数必须 >= 1。
  // - unref() 递减；返回 true 表示计数刚归零，调用方负责 delete。
  // - ref()/unref()/refcount() 都是 const；refcount_ 为 mutable atomic。
  // - 内存序：add 用 relaxed（已持有引用者增加计数无需同步）；sub 用 acq_rel
  //   （release 保证对象写入先于最终 delete 可见，acquire 保证最后一个释放者看到全部写入）；
  //   load 用 relaxed（诊断用，不建立同步）。M9 单线程可用，M10 跨线程仍安全。
  // - 析构断言 refcount()==0，捕获“还有引用却直接 delete”的所有权 bug。
  // - 不解决循环引用；WeakRef 推迟。
  class RefCounted : public Object {
  public:
    RefCounted() = default;
    ~RefCounted() override;

    // Object 已禁拷贝/移动；这里显式重申，避免派生类误以为可以值语义复制。
    RefCounted(const RefCounted&) = delete;
    RefCounted& operator=(const RefCounted&) = delete;
    RefCounted(RefCounted&&) = delete;
    RefCounted& operator=(RefCounted&&) = delete;

    void ref() const noexcept;
    [[nodiscard]] bool unref() const noexcept;
    [[nodiscard]] std::uint32_t refcount() const noexcept;

  private:
    mutable std::atomic<std::uint32_t> refcount_{0};
  };

} // namespace yr::obj