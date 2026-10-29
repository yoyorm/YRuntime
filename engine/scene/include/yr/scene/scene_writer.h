#pragma once

#include <ostream>
#include <string>

namespace yr::scene {

  class Node;
  class PackedScene;

  // 失败时 text 为空，error 说明具体节点与问题；成功时 text 是完整 canonical 文本。
  struct SceneWriteResult {
    bool ok = false;
    std::string text;
    std::string error;
  };

  // 从 Node 树 / PackedScene 写 canonical .scn。
  // Node 版本内部先构造 IR（ext/sub/connection 为空），再走 PackedScene 版本。
  // 写前校验：ext/sub id/type/path、引用 index 与 id、connection、node 树；
  // 失败返回 SceneWriteResult{ok=false, error}，不产出半份 text。
  class SceneWriter {
  public:
    [[nodiscard]] static SceneWriteResult write(const Node& root);
    [[nodiscard]] static SceneWriteResult write(const PackedScene& scene);

    // 便捷 ostream 版本：先取得完整 text 再写出，校验失败时 out 不被写入。
    [[nodiscard]] static bool write(const Node& root, std::ostream& out, std::string& error);
  };

} // namespace yr::scene
