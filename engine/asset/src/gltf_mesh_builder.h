#pragma once

#include <string>

#include <yr/asset/gltf_loader.h>

namespace yr::asset {

  // 只允许主线程调用；job.source 必须非空。
  // 填充 out.vertices / out.indices / out.bounds*；不改变 Resource 状态。
  [[nodiscard]] bool buildGltfMesh(const GltfDeferredMesh& job, Mesh& out, std::string& error);

} // namespace yr::asset