#pragma once

namespace yr::gameplay {

  // 注册 InputAxis / MouseLook / KeyTrigger / MoveMotor / FlyMotor / BoxCollider / Counter / SunSweep。
  // 必须在 register_scene_classes() 之后调用。
  void register_gameplay_classes();

} // namespace yr::gameplay