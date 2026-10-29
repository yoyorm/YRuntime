#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <yr/object/variant.h>
#include <yr/scene/scene_diagnostic.h>

namespace yr::scene {

  class PackedScene;

  inline constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);
  // M8a 兼容别名。
  inline constexpr std::size_t kInvalidParent = kInvalidIndex;

  enum class ReferenceKind : std::uint8_t {
    kNone,
    kExtResource,
    kSubResource,
  };

  // 文件里存 id，解析后填 index；source_* 仅用于诊断，不序列化。
  struct SceneReference {
    ReferenceKind kind = ReferenceKind::kNone;
    std::string id;
    std::size_t index = kInvalidIndex;
    int source_line = 0;
    int source_column = 0;

    [[nodiscard]] bool isReference() const noexcept { return kind != ReferenceKind::kNone; }
  };

  // kind == kNone 时用 value；否则用 reference。
  struct SceneValue {
    obj::Variant value;
    SceneReference reference;

    [[nodiscard]] bool isReference() const noexcept { return reference.isReference(); }
  };

  struct ResolveResult {
    bool ok = true;
    std::vector<SceneDiagnostic> diagnostics;
  };

  // 两遍解析：建 ext/sub id 表 → 回填 SceneReference.index → DFS 检测 sub_resource 环。
  [[nodiscard]] ResolveResult resolveReferences(PackedScene& scene);

} // namespace yr::scene
