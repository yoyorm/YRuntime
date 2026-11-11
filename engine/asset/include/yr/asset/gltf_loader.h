#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <yr/asset/resource_types.h>
#include <yr/math/transform.h>
#include <yr/object/ref.h>

namespace yr::asset {

  struct GltfPrimitive {
    obj::Ref<Mesh> mesh;
    obj::Ref<Material> material;
  };

  struct GltfNode {
    std::string name;
    math::Transform localTransform{}; // 只含本节点 T*R*S，不乘父变换
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

  // find(uid) 命中已有资源；store(resource) 注册新资源，返回 false 表示 UID 冲突/注册失败。
  struct GltfResourceHooks {
    std::function<obj::Ref<Resource>(AssetUID)> find;
    std::function<bool(obj::Ref<Resource>)> store;
  };

  // resPath 必须是 canonical res:// 路径；assetRoot 是 res:// 根对应的文件系统目录。
  // 同步读取；不碰 GPU。传入 hooks 时按 UID 复用/注册 Mesh、Material、Texture。
  [[nodiscard]] GltfLoadResult loadGltfScene(std::string_view resPath, std::string_view assetRoot,
                                             const GltfResourceHooks* hooks = nullptr);

} // namespace yr::asset
