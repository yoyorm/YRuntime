#pragma once

#include <cstddef>

#include <yr/core/handle.h>
#include <yr/core/slot_map.h>
#include <yr/math/mat4.h>

namespace yr::scene {

  // Transform 的派生状态：只保存 global 缓存与脏标记
  struct TransformData {
    math::Mat4 global;
    bool localDirty = true;
  };

  class TransformStore {
  public:
    [[nodiscard]] core::Handle<TransformData> create() { return slots_.insert(TransformData{}); }

    void destroy(const core::Handle<TransformData>& handle) noexcept { (void)slots_.erase(handle); }

    [[nodiscard]] TransformData* get(const core::Handle<TransformData>& handle) noexcept { return slots_.get(handle); }

    [[nodiscard]] const TransformData* get(const core::Handle<TransformData>& handle) const noexcept {
      return slots_.get(handle);
    }

    [[nodiscard]] std::size_t size() const noexcept { return slots_.size(); }

  private:
    core::SlotMap<TransformData> slots_;
  };

} // namespace yr::scene
