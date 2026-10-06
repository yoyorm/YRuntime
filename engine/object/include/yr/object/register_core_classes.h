#pragma once

namespace yr::obj {

  // 显式注册引擎核心类。必须在 main 最早处调用一次。
  // 游戏 / 插件类在它之后注册；注册完成后由调用方决定何时 globalClassDB().freeze()。
  void register_core_classes();

} // namespace yr::obj