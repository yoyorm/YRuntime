#pragma once

#include <cstddef>

#include <yr/core/handle.h>
#include <yr/core/slot_map.h>
#include <yr/math/mat4.h>

namespace yr::scene {

  // Transform 的最小状态（仅平移）。position 是唯一可写数据源（在 Node 中），本结构只保存
  // 派生缓存与脏标记。不存 local 矩阵：refresh 时按需合成，避免 position 与 local 双源。
  struct TransformData {
    math::Mat4 global;
    bool localDirty = true;
  };

  // Node 只持有 Handle，经 SceneTree 转发访问；槽的分配（首次入树）与释放（Node 析构）
  // 由 Node 驱动，detach 保留。
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