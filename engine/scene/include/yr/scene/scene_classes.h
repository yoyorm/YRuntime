#pragma once

namespace yr::obj {
  class ClassInfo;
}

namespace yr::scene {

  // Node 的静态 ClassInfo
  [[nodiscard]] const yr::obj::ClassInfo& nodeClassInfo() noexcept;

  // 注册 Node（并确保 Object 已注册）
  void register_scene_classes();

} // namespace yr::scene