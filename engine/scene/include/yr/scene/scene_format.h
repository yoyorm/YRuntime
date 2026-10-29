#pragma once

#include <string_view>

namespace yr::scene {

  // .scn 场景文本格式。本头注释是格式的唯一真相源。
  //
  // 命名：文本 .scn；二进制 .scnb（M8c 才实现，这里只保留扩展名常量）。
  // 编码：UTF-8；读取忽略开头 BOM，保存不写；读取接受 LF/CRLF，保存一律 LF。
  // 注释：'#' 到行尾（引号内除外）；空行忽略。
  //
  // 段顺序（canonical）：
  //   [scene]
  //   [ext_resource]*        # 必须在任何 node 之前
  //   [sub_resource]*        # 必须在任何 node 之前
  //   [node]*                # 前序，nodes[0] 是根
  //   [connection]*          # 必须在所有 node 之后
  // 每个 section 之间空一行，文件末尾一个换行。
  //
  // [scene]：
  // - 必须第一个且唯一，没有字段。
  // - 出现任何字段 → warning + 跳过（为将来保留，不做版本号）。
  //
  // [ext_resource]（外部资源，叶子）：
  //   [ext_resource type="Material" id="mat_ground" path="res://assets/ground.yrres" uid="..."]
  // - type 必填非空；id 必填、isValidReferenceId、同 ext 表唯一。
  // - path 必填、非空、以 "res://" 开头；uid 可选。
  // - 未知字段 warning；属性行 → error。
  //
  // [sub_resource]（内嵌资源）：
  //   [sub_resource type="Material" id="mat_local"]
  //   albedo = ExtResource("tex_1")
  //   base = SubResource("mat_ground")
  // - type 必填非空；id 必填、isValidReferenceId、同 sub 表唯一。
  // - 属性名规则同 node：[A-Za-z_][A-Za-z0-9_]*；重复属性 → error。
  // - 值可以是普通 Variant，或 ExtResource("id") / SubResource("id")；括号内允许空格。
  // - 不存在的引用 id、自引用、sub_resource 环 → error（由 resolveReferences 报）。
  // - 属性行只属于当前 sub_resource；下一个 section 出现即结束。
  //
  // [node]：
  //   [node name="Root" type="Node"]
  //   position = Vector3(0, 0, 0)
  // - name 必填、非空、不是 "." / ".."、同级唯一；不允许 '/'、'"'、换行；允许 UTF-8。
  // - type 必填，必须是 ClassDB 中已注册的 Node 派生类；当前只有 Node。
  // - 根节点不写 parent；其他节点必填： "." = 根， "A/B" = 相对根、不含根名的父路径。
  // - 父必须在子之前出现（前序）；不写 index；同级顺序 = 文件顺序。
  // - 父路径不存在 / 父在子后 / 父环 / 同级重名 / 未知 type → error。
  // - 属性行规则：findProperty 解析；未知属性 warning；重复属性 error；
  //   只读属性 warning + 跳过。node 属性值不允许引用语法 → error。
  //
  // [connection]：
  //   [connection from="Player" to="Hat" signal="hit" method="on_hit"]
  // - from / to 必填，node path： "." = 根， "Player/Hat" = 相对根、不含根名。
  // - signal / method 必填非空，intern 成 StringId。
  // - from / to 必须指向已出现的 node；重复 connection（同四元组）→ error。
  // - 未知字段 warning；属性行 → error；出现在 node 之前 → error。
  //
  // 值语法：true/false、int、double、string、Vector3(x, y, z)。
  // - 字符串转义 \" \\ \n \t \r；不允许跨行；UTF-8 原样保存。
  // - double 用 max_digits10=17、float 用 max_digits10=9 保存，确保 round-trip。
  // - 拒绝 NaN/Inf；Vector3 分量按 double 解析后转 float；不接受尾逗号。
  // - 引用只存在于 sub_resource 属性，不进入 Variant。
  //
  // 保存规范形式：scene → ext → sub → node（前序）→ connection；section 间空行；
  // node 属性：Node 树版本按 ClassInfo 顺序（基类在前），IR 版本保持存储顺序
  // （reader 保持文件顺序）；sub_resource 属性按属性名升序；
  // 写全部 kSerialized 属性；不写注释；文件末尾一个换行；
  // save -> load -> save 必须逐字节相同。
  // 保存前校验（id / path / 引用 / connection / node 树）；失败返回
  // SceneWriteResult{ok=false, error}，不产出半份 text。
  //
  // 诊断：line/column 都是 1-based；error 使加载失败，warning 继续。
  // 未知 section → warning + 跳过。
  //
  // 暂不做：版本号/迁移、.scnb、JSON、NodePath 属性、通用 NodeRef 属性值、
  // rotation/scale、默认值省略、编辑器/异步加载、游戏存档。
  // NodeRef 只用于 [connection] 的 from/to，不作为任意属性值。

  inline constexpr std::string_view kSceneHeader = "[scene]";
  inline constexpr std::string_view kSceneTextExtension = ".scn";
  inline constexpr std::string_view kSceneBinaryExtension = ".scnb";

  // node name 的唯一规则源：reader 校验与 writer 保存共用，避免两套规则漂移。
  // "." / ".." 保留给 parent path（"." = 根），不能作为节点名。
  [[nodiscard]] inline bool isValidNodeName(std::string_view name) noexcept {
    if (name.empty() || name == "." || name == "..") {
      return false;
    }
    return name.find('/') == std::string_view::npos && name.find('"') == std::string_view::npos &&
           name.find('\n') == std::string_view::npos && name.find('\r') == std::string_view::npos;
  }

  // 属性名：reader 与 writer 共用。
  [[nodiscard]] inline bool isValidPropertyName(std::string_view name) noexcept {
    if (name.empty()) {
      return false;
    }
    const auto isStart = [](char c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    const auto isChar = [&isStart](char c) noexcept { return isStart(c) || (c >= '0' && c <= '9'); };
    if (!isStart(name.front())) {
      return false;
    }
    for (const char c : name) {
      if (!isChar(c)) {
        return false;
      }
    }
    return true;
  }

  // ext/sub resource id：非空，只允许 [A-Za-z0-9_.-]。
  [[nodiscard]] inline bool isValidReferenceId(std::string_view id) noexcept {
    if (id.empty()) {
      return false;
    }
    for (const char c : id) {
      const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                           c == '.' || c == '-';
      if (!allowed) {
        return false;
      }
    }
    return true;
  }

} // namespace yr::scene
