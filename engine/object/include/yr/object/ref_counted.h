#pragma once

#include <atomic>
#include <cstdint>

#include <yr/object/object.h>

namespace yr::obj {

  // 引用计数基类。
  // - 不解决循环引用；WeakRef 推迟。
  class RefCounted : public Object {
  public:
    RefCounted() = default;
    ~RefCounted() override;

    // Object 已禁拷贝/移动
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