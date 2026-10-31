#pragma once

#include <atomic>
#include <cstdint>

#include <yr/object/object.h>

namespace yr::obj {

  // M9-0 脚手架：引用计数基类。计数语义由用户手写，当前实现是 TODO。
  //
  // - 构造时 refcount 为 0；Ref<T> 持有时计数必须 >= 1。
  // - unref() 递减；返回 true 表示计数刚归零，调用方负责 delete。
  // - ref()/unref()/refcount() 都是 const；refcount_ 为 mutable atomic。
  // - 内存序待用户决定：单线程可 relaxed；跨线程释放/获取需要 acquire/release 或更强。
  // - 不解决循环引用；WeakRef 推迟。
  class RefCounted : public Object {
  public:
    RefCounted() = default;
    ~RefCounted() override = default;

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