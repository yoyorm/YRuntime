#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include <yr/object/ref_counted.h>

namespace yr::obj {

  // M9-0 脚手架：强引用智能指针。计数与释放语义由用户手写，当前成员函数是 TODO。
  //
  // 契约：
  // - Ref<T> 持有时 refcount >= 1；拷贝 ref()，移动偷指针不改变计数。
  // - 删除时机 = unref() 返回 true；reset()/析构/赋值替换旧值时都必须遵守。
  // - 不支持循环引用；WeakRef 推迟。
  template <typename T> class Ref {
    static_assert(std::is_base_of_v<RefCounted, T>, "Ref<T>: T must derive from RefCounted");

    template <typename U> friend class Ref;

  public:
    Ref() noexcept = default;

    explicit Ref(T* ptr) noexcept : ptr_(ptr) {
      // TODO(user): 接管所有权并 ref()。
    }

    Ref(const Ref& other) noexcept : ptr_(other.ptr_) {
      // TODO(user): 拷贝构造：ref()。
    }

    Ref(Ref&& other) noexcept : ptr_(other.ptr_) {
      // TODO(user): 移动构造：偷指针、不改变计数。
      other.ptr_ = nullptr;
    }

    template <typename U>
      requires std::is_convertible_v<U*, T*>
    Ref(const Ref<U>& other) noexcept : ptr_(other.ptr_) {
      // TODO(user): 转换拷贝：ref()。
    }

    template <typename U>
      requires std::is_convertible_v<U*, T*>
    Ref(Ref<U>&& other) noexcept : ptr_(other.ptr_) {
      // TODO(user): 转换移动：偷指针、不改变计数。
      other.ptr_ = nullptr;
    }

    ~Ref() {
      // TODO(user): unref() 返回 true 时 delete。
    }

    Ref& operator=(const Ref& other) noexcept {
      // TODO(user): 自赋值检查；ref() 新值；unref() 旧值并在归零时 delete。
      ptr_ = other.ptr_;
      return *this;
    }

    Ref& operator=(Ref&& other) noexcept {
      // TODO(user): 自赋值检查；unref() 旧值并在归零时 delete；偷指针。
      if (this != &other) {
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
      }
      return *this;
    }

    void reset() noexcept {
      // TODO(user): unref() 返回 true 时 delete；然后 ptr_ = nullptr。
    }

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }
    [[nodiscard]] T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] bool valid() const noexcept { return ptr_ != nullptr; }

  private:
    T* ptr_ = nullptr;
  };

  template <typename T, typename... Args> [[nodiscard]] Ref<T> make_ref(Args&&... args) {
    static_assert(std::is_base_of_v<RefCounted, T>, "make_ref<T>: T must derive from RefCounted");
    // TODO(user): make_unique<T>(...).release() 后交给 Ref<T> 接管（当前 Ref 构造函数只存指针）。
    return Ref<T>(std::make_unique<T>(std::forward<Args>(args)...).release());
  }

} // namespace yr::obj