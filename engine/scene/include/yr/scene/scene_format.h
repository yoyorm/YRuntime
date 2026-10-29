#pragma once

#include <string_view>

namespace yr::scene {

  // .scn Scene v1（文本）。
  //
  // 命名：文本 .scn；二进制 .scnb（v1 只保留扩展名常量，不实现）。
  // UTF-8；读取忽略开头 BOM，保存不写；读取接受 LF/CRLF，保存一律 LF。
  // '#' 到行尾是注释（字符串内除外）；空行忽略。
  //
  // 文件结构（节点按前序排列）：
  //   [scene format=1]
  //
  //   [node name="Root" type="Node"]
  //   position = Vector3(0, 0, 0)
  //
  //   [node name="Player" type="Node" parent="."]
  //   position = Vector3(0, 1, 0)
  //
  //   [node name="Hat" type="Node" parent="Player"]
  //   position = Vector3(0, 0.5, 0)
  //
  // Scene header：
  // - 第一个 section 必须是且只能是一个 [scene format=1]；format 只支持 1。
  // - 未知 header 字段 warning + 跳过；缺少/重复 header、缺 format、format != 1 → error。
  //
  // Node section：
  // - name 必填、非空、不是 "." / ".."、同级唯一；不允许 '/'、'"'、换行；允许 UTF-8。
  // - type 必填，必须是 ClassDB 中已注册的 Node 派生类；v1 只有 Node。
  // - 根节点不写 parent；其他节点必填： "." = 根， "A/B" = 相对根的父路径。
  // - 父必须在子之前出现（前序）；不写 index；同级顺序 = 文件顺序。
  // - 父路径不存在 / 父在子后 / 父环 / 同级重名 / 未知 type → error。
  // - 未知 section warning + 跳过。
  //
  // Properties：
  // - 形式 name = value；通过 ClassInfo::findProperty 解析；只写 kSerialized 属性。
  // - 未知属性 warning + 跳过；重复属性 error；属性行出现在 node 前 → error；
  //   只读属性 warning + 跳过。
  // - v1 的 Node 只有 position = Vector3(x, y, z)，缺省 (0, 0, 0)。
  //
  // 值语法：true/false、int、double、string、Vector3(x, y, z)。
  // - 字符串转义 \" \\ \n \t \r；不允许跨行；UTF-8 原样保存。
  // - double 用 max_digits10=17、float 用 max_digits10=9 保存，确保 round-trip。
  // - 拒绝 NaN/Inf；Vector3 分量按 double 解析后转 float；v1 不接受尾逗号。
  //
  // 保存规范形式：前序；每个 node 之间空一行；属性按 ClassInfo 顺序（基类在前）；
  // v1 写全部 kSerialized 属性；不写注释；文件末尾一个换行；
  // save -> load -> save 必须逐字节相同。
  // 保存前校验全树（name 规则、同级唯一、root 无 parent、serialized 属性可编码）；
  // 任一项失败返回 SceneWriteResult{ok=false, error}，不产出半份 text。
  //
  // 诊断：line/column 都是 1-based；error 使加载失败，warning 继续。
  //
  // v1 不做：.scnb、JSON、ExtResource/SubResource/NodeRef、connections、NodePath 属性、
  // rotation/scale、默认值省略、migrator、编辑器/异步加载、游戏存档。

  inline constexpr std::string_view kSceneHeader = "[scene format=1]";
  inline constexpr int kSceneFormatVersion = 1;
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

} // namespace yr::scene