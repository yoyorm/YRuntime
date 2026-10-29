#include <yr/scene/scene_reference.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <yr/scene/packed_scene.h>

namespace yr::scene {

  namespace {

    void addDiagnostic(ResolveResult& result, SceneDiagnostic::Severity severity, int line, int column,
                       std::string message) {
      result.diagnostics.push_back(SceneDiagnostic{severity, line, column, std::move(message)});
    }

    [[nodiscard]] int sourceLine(const SceneReference& reference) noexcept {
      return reference.source_line > 0 ? reference.source_line : 1;
    }

    [[nodiscard]] int sourceColumn(const SceneReference& reference) noexcept {
      return reference.source_column > 0 ? reference.source_column : 1;
    }

  } // namespace

  ResolveResult resolveReferences(PackedScene& scene) {
    ResolveResult result;

    // 1. 建 id -> index 表；重复 id 在这里统一报，reader 不重复报。
    std::unordered_map<std::string, std::size_t> extById;
    extById.reserve(scene.ext_resources.size());
    for (std::size_t index = 0; index < scene.ext_resources.size(); ++index) {
      const std::string& id = scene.ext_resources[index].id;
      if (!extById.emplace(id, index).second) {
        addDiagnostic(result, SceneDiagnostic::Severity::kError, 1, 1, "重复的 ext_resource id '" + id + "'");
      }
    }

    std::unordered_map<std::string, std::size_t> subById;
    subById.reserve(scene.sub_resources.size());
    for (std::size_t index = 0; index < scene.sub_resources.size(); ++index) {
      const std::string& id = scene.sub_resources[index].id;
      if (!subById.emplace(id, index).second) {
        addDiagnostic(result, SceneDiagnostic::Severity::kError, 1, 1, "重复的 sub_resource id '" + id + "'");
      }
    }

    // 2. 回填引用 index；不存在的 id 立即报 error。
    for (PackedSubResource& subResource : scene.sub_resources) {
      for (PackedSubResourceProperty& property : subResource.properties) {
        SceneReference& reference = property.value.reference;
        if (!reference.isReference()) {
          continue;
        }

        if (reference.kind == ReferenceKind::kExtResource) {
          const auto found = extById.find(reference.id);
          if (found == extById.end()) {
            addDiagnostic(result, SceneDiagnostic::Severity::kError, sourceLine(reference), sourceColumn(reference),
                          "ext_resource 引用不存在：'" + reference.id + "'");
          } else {
            reference.index = found->second;
          }
        } else if (reference.kind == ReferenceKind::kSubResource) {
          const auto found = subById.find(reference.id);
          if (found == subById.end()) {
            addDiagnostic(result, SceneDiagnostic::Severity::kError, sourceLine(reference), sourceColumn(reference),
                          "sub_resource 引用不存在：'" + reference.id + "'");
          } else {
            reference.index = found->second;
          }
        } else {
          addDiagnostic(result, SceneDiagnostic::Severity::kError, sourceLine(reference), sourceColumn(reference),
                        "未知的引用类型");
        }
      }
    }

    // 3. 只沿 sub_resource 引用做三色环检测；只报第一个环。
    struct Edge {
      std::size_t target = kInvalidIndex;
      int line = 1;
      int column = 1;
    };

    std::vector<std::vector<Edge>> graph(scene.sub_resources.size());
    for (std::size_t index = 0; index < scene.sub_resources.size(); ++index) {
      for (const PackedSubResourceProperty& property : scene.sub_resources[index].properties) {
        const SceneReference& reference = property.value.reference;
        if (reference.kind == ReferenceKind::kSubResource && reference.index != kInvalidIndex) {
          graph[index].push_back(Edge{reference.index, sourceLine(reference), sourceColumn(reference)});
        }
      }
    }

    std::vector<std::uint8_t> color(scene.sub_resources.size(), 0); // 0 未访问 / 1 访问中 / 2 完成
    std::vector<std::size_t> stack;
    bool cycleReported = false;

    const auto visit = [&](const auto& self, std::size_t node) -> void {
      color[node] = 1;
      stack.push_back(node);

      for (const Edge& edge : graph[node]) {
        if (cycleReported) {
          break;
        }
        if (color[edge.target] == 1) {
          const auto cycleStart = std::find(stack.begin(), stack.end(), edge.target);
          std::string path;
          for (auto it = cycleStart; it != stack.end(); ++it) {
            if (!path.empty()) {
              path += " -> ";
            }
            path += scene.sub_resources[*it].id;
          }
          path += " -> " + scene.sub_resources[edge.target].id;
          addDiagnostic(result, SceneDiagnostic::Severity::kError, edge.line, edge.column,
                        "sub_resource 引用环：" + path);
          cycleReported = true;
          break;
        }
        if (color[edge.target] == 0) {
          self(self, edge.target);
        }
      }

      stack.pop_back();
      color[node] = 2;
    };

    for (std::size_t index = 0; index < scene.sub_resources.size() && !cycleReported; ++index) {
      if (color[index] == 0) {
        visit(visit, index);
      }
    }

    for (const SceneDiagnostic& diagnostic : result.diagnostics) {
      if (diagnostic.severity == SceneDiagnostic::Severity::kError) {
        result.ok = false;
        break;
      }
    }
    return result;
  }

} // namespace yr::scene
