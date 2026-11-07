#pragma once

#include <unordered_set>

#include <yr/asset/asset_uid.h>
#include <yr/render/snapshot.h>

namespace yr::scene {
  class SceneTree;
}
namespace yr::engine {

  // SceneTree → RenderSnapshot 的唯一单向转换点（D7 规则②）。M5 全量抽取、不做剔除。
  class RenderExtractSystem {
  public:
    // 按前序抽取可绘节点并生成资源增量；不修改 out.frame_index。
    void extract(const scene::SceneTree& tree, const render::CameraPod& camera, render::RenderSnapshot& out);
    void reset() noexcept;

  private:
    std::unordered_set<asset::AssetUID> residentMeshes_;
    std::unordered_set<asset::AssetUID> residentMaterials_;
    std::unordered_set<asset::AssetUID> residentTextures_;
  };

} // namespace yr::engine
