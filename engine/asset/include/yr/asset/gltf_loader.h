#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <yr/asset/resource_types.h>
#include <yr/math/mat4.h>
#include <yr/object/ref.h>

namespace yr::asset {

  struct GltfPrimitive {
    obj::Ref<Mesh> mesh;
    obj::Ref<Material> material;
  };

  struct GltfNode {
    std::string name;
    math::Mat4 localTransform{}; // 只含本节点 T*R*S，不乘父变换
    std::vector<GltfPrimitive> primitives;
    std::vector<GltfNode> children;
  };

  struct GltfScene {
    std::string name;
    std::vector<GltfNode> roots;
  };

  struct GltfLoadResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;
    GltfScene scene;

    explicit operator bool() const noexcept { return ok; }
  };

  // resPath 必须是 canonical res:// 路径；assetRoot 是 res:// 根对应的文件系统目录。
  // 同步读取；不缓存、不接 AssetDatabase、不碰 GPU。
  [[nodiscard]] GltfLoadResult loadGltfScene(std::string_view resPath, std::string_view assetRoot);

} // namespace yr::asset