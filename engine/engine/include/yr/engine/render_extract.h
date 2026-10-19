#pragma once

#include <yr/render/snapshot.h>

namespace yr::scene {
  class SceneTree;
}
namespace yr::engine {

  // SceneTree → RenderSnapshot 的唯一单向转换点（D7 规则②）。M5 全量抽取、不做剔除。
  class RenderExtractSystem {
  public:
    // 重置 out.camera 为固定默认（identity），按前序清空并填充 out.items；不修改 out.frame_index。
    void extract(const scene::SceneTree& tree, render::RenderSnapshot& out) const;
  };

} // namespace yr::engine