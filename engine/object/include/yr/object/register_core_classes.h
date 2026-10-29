#pragma once

namespace yr::obj {

  class ClassInfo;

  // 显式注册引擎核心类。幂等：可重复调用；必须在 main 最早处调用一次。
  // 游戏 / 插件类在它之后注册；注册完成后由调用方决定何时 globalClassDB().freeze()。
  void register_core_classes();

  // Object 的静态 ClassInfo（父为 nullptr、无属性、无工厂）。Node 等下层类用它作为 parent。
  [[nodiscard]] const ClassInfo& objectClassInfo() noexcept;

} // namespace yr::obj