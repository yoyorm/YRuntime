#pragma once

#include <istream>
#include <string_view>
#include <vector>

#include <yr/scene/packed_scene.h>
#include <yr/scene/scene_diagnostic.h>

namespace yr::obj {
  class ClassDB;
}

namespace yr::scene {

  struct SceneLoadResult {
    PackedScene scene;
    std::vector<SceneDiagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept {
      for (const SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == SceneDiagnostic::Severity::kError) {
          return true;
        }
      }
      return false;
    }

    [[nodiscard]] bool ok() const noexcept { return !hasErrors(); }
  };

  class SceneReader {
  public:
    explicit SceneReader(const obj::ClassDB& classes) noexcept : classes_(classes) {}

    [[nodiscard]] SceneLoadResult read(std::string_view text) const;
    [[nodiscard]] SceneLoadResult read(std::istream& stream) const;

  private:
    const obj::ClassDB& classes_;
  };

} // namespace yr::scene