#pragma once

namespace yr::obj {

  class ClassInfo;

  // 显式注册引擎核心类
  void register_core_classes();

  // Object 的静态 ClassInfo（父为 nullptr、无属性、无工厂）。Node 等下层类用它作为 parent。
  [[nodiscard]] const ClassInfo& objectClassInfo() noexcept;

} // namespace yr::obj