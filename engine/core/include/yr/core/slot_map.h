#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <yr/core/assert.h>
#include <yr/core/handle.h>
namespace yr::core {

  template <typename T> class SlotMap {
  public:
    // 增
    Handle<T> insert(T val) {
      uint32_t slot;
      if (freelist_.empty()) {
        slot = next_slot_++;
        YR_ASSERT(slot <= HandleBits::kIndexMask); // 防止高位溢出范围
        slot2dense_.emplace_back(kInvalidDense);
        generation_.emplace_back(1);
      } else {
        slot = freelist_.back();
        freelist_.pop_back();
        generation_[slot]++;
      }
      const uint32_t pos = static_cast<uint32_t>(dense_.size()); // 先得到size 再 push，无需后续size -1
      dense_.emplace_back(std::move(val));
      dense2slot_.emplace_back(slot);
      slot2dense_[slot] = pos;
      return Handle<T>::make(slot, generation_[slot]);
    }

    // 删
    bool erase(const Handle<T>& handle) {
      if (!contains(handle))
        return false;

      const uint32_t slot = handle.index();

      const uint32_t pos = slot2dense_[slot];
      const uint32_t last = static_cast<uint32_t>(dense_.size()) - 1;

      if (pos != last) { // 只有当被删的不是尾巴时才搬运
        const uint32_t moved_slot = dense2slot_[last];
        dense_[pos] = std::move(dense_[last]); // 比 swap 少一次 move
        dense2slot_[pos] = moved_slot;
        slot2dense_[moved_slot] = pos;
      }

      slot2dense_[slot] = kInvalidDense;
      dense_.pop_back();
      dense2slot_.pop_back();
      freelist_.push_back(slot);
      return true;
    }

    // 查询
    [[nodiscard]] bool contains(const Handle<T>& handle) const noexcept {
      if (!handle.valid()) {
        return false;
      }
      const uint32_t slot = handle.index();
      if (slot >= generation_.size() || slot2dense_[slot] == kInvalidDense) {
        return false;
      }
      return generation_[slot] == handle.generation();
    }

    [[nodiscard]] const T* get(const Handle<T>& handle) const noexcept {
      if (!contains(handle)) {
        return nullptr;
      }
      return &dense_[slot2dense_[handle.index()]];
    }

    [[nodiscard]] T* get(const Handle<T>& handle) noexcept { return const_cast<T*>(std::as_const(*this).get(handle)); }

    [[nodiscard]] T& operator[](const Handle<T>& handle) {
      YR_ASSERT(contains(handle));
      return dense_[slot2dense_[handle.index()]];
    }

    [[nodiscard]] const T& operator[](const Handle<T>& handle) const {
      YR_ASSERT(contains(handle));
      return dense_[slot2dense_[handle.index()]];
    }

    [[nodiscard]] Handle<T> handleAt(uint32_t pos) const noexcept {
      YR_ASSERT(pos < dense_.size());
      const uint32_t slot = dense2slot_[pos];
      return Handle<T>::make(slot, generation_[slot]);
    }

    [[nodiscard]] std::size_t size() const noexcept { return dense_.size(); }
    [[nodiscard]] bool empty() const noexcept { return dense_.empty(); }

    /// 预留容量，避免增长过程中反复扩容搬移。
    void reserve(std::size_t n) {
      dense_.reserve(n);
      dense2slot_.reserve(n);
      generation_.reserve(n);
      slot2dense_.reserve(n);
      freelist_.reserve(n);
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return dense_.capacity(); }

    // 清空
    void clear() {
      // 不能重置计数器，并且需要++generation 来让旧handle失效
      for (std::size_t slot = 0; slot < slot2dense_.size(); ++slot) {
        if (slot2dense_[slot] != kInvalidDense) {
          ++generation_[slot];
          slot2dense_[slot] = kInvalidDense;
          freelist_.push_back(static_cast<uint32_t>(slot));
        }
      }
      dense_.clear();
      dense2slot_.clear();
    }

    // 迭代
    [[nodiscard]] const T* data() const noexcept { return dense_.data(); }

    using const_iterator = typename std::vector<T>::const_iterator;
    [[nodiscard]] const_iterator begin() const noexcept { return dense_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return dense_.end(); }

  private:
    static constexpr uint32_t kInvalidDense = 0xFFFFFFFFu;

    uint32_t next_slot_ = 0;
    std::vector<T> dense_;
    std::vector<uint64_t> generation_; // 默认gen = 1开始
    std::vector<uint32_t> slot2dense_;
    std::vector<uint32_t> dense2slot_;
    std::vector<uint32_t> freelist_; // 空闲Slot
  };

} // namespace yr::core