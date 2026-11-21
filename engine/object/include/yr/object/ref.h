#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include <yr/object/ref_counted.h>

namespace yr::obj {

  // 强引用智能指针。
  // - 不支持循环引用；WeakRef 推迟

  template <typename T> class Ref {
    static_assert(std::is_base_of_v<RefCounted, T>, "Ref<T>: T must derive from RefCounted");

    template <typename U> friend class Ref;

  public:
    Ref() noexcept = default;

    explicit Ref(T* ptr) noexcept : ptr_(ptr) { retain(); }

    Ref(const Ref& other) noexcept : ptr_(other.ptr_) { retain(); }

    Ref(Ref&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

    template <typename U>
      requires std::is_convertible_v<U*, T*>
    Ref(const Ref<U>& other) noexcept : ptr_(static_cast<T*>(other.ptr_)) {
      retain();
    }

    template <typename U>
      requires std::is_convertible_v<U*, T*>
    Ref(Ref<U>&& other) noexcept : ptr_(static_cast<T*>(other.ptr_)) {
      other.ptr_ = nullptr;
    }

    ~Ref() { release(); }

    // 转移所有权时先 ref() 新值再释放旧值
    Ref& operator=(const Ref& other) noexcept {
      if (this != &other) {
        T* const incoming = other.ptr_;
        if (incoming != nullptr) {
          incoming->ref();
        }
        release();
        ptr_ = incoming;
      }
      return *this;
    }

    Ref& operator=(Ref&& other) noexcept {
      if (this != &other) {
        release();
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
      }
      return *this;
    }

    void reset() noexcept { release(); }

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }
    [[nodiscard]] T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] bool valid() const noexcept { return ptr_ != nullptr; }

  private:
    void retain() const noexcept {
      if (ptr_ != nullptr) {
        ptr_->ref();
      }
    }

    // 统一的“放弃当前所有权”路径：unref() 返回 true 才 delete，随后置空。
    void release() noexcept {
      T* const old = ptr_;
      ptr_ = nullptr;
      if (old != nullptr && old->unref()) {
        std::default_delete<T>{}(old);
      }
    }

    T* ptr_ = nullptr;
  };

  template <typename T, typename... Args> [[nodiscard]] Ref<T> make_ref(Args&&... args) {
    static_assert(std::is_base_of_v<RefCounted, T>, "make_ref<T>: T must derive from RefCounted");
    return Ref<T>(std::make_unique<T>(std::forward<Args>(args)...).release());
  }

} // namespace yr::obj