#pragma once

namespace yr::gameplay {

  // 注册 InputAxis / KeyTrigger / MoveMotor / BoxCollider / Counter。
  // 必须在 register_scene_classes() 之后调用。
  void register_gameplay_classes();

} // namespace yr::gameplay