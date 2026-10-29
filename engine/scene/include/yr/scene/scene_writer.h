#pragma once

#include <ostream>
#include <string>

namespace yr::scene {

  class Node;

  // 失败时 text 为空，error 说明具体节点与问题；成功时 text 是完整 canonical 文本。
  struct SceneWriteResult {
    bool ok = false;
    std::string text;
    std::string error;
  };

  // 从 Node 树写 canonical .scn。属性顺序来自 ClassInfo（基类在前），只写 kSerialized。
  // 写前校验整棵树：name 规则、同级唯一、root 无 parent、classInfo 是 Node、
  // serialized 属性有 getter 且可编码。校验失败不产出半份 text。
  class SceneWriter {
  public:
    [[nodiscard]] static SceneWriteResult write(const Node& root);

    // 便捷 ostream 版本：先取得完整 text 再写出，校验失败时 out 不被写入。
    [[nodiscard]] static bool write(const Node& root, std::ostream& out, std::string& error);
  };

} // namespace yr::scene