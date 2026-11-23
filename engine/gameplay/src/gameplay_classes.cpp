#include <yr/gameplay/gameplay_classes.h>

#include <yr/gameplay/box_collider.h>
#include <yr/gameplay/counter.h>
#include <yr/gameplay/input_axis.h>
#include <yr/gameplay/key_trigger.h>
#include <yr/gameplay/mouse_look.h>
#include <yr/gameplay/move_motor.h>
#include <yr/gameplay/sun_sweep.h>
#include <yr/object/class_db.h>
#include <yr/scene/scene_classes.h>

namespace yr::gameplay {

  void register_gameplay_classes() {
    static bool registered = false;
    if (registered) {
      return;
    }
    scene::register_scene_classes();
    obj::globalClassDB().registerClass(inputAxisClassInfo());
    obj::globalClassDB().registerClass(mouseLookClassInfo());
    obj::globalClassDB().registerClass(keyTriggerClassInfo());
    obj::globalClassDB().registerClass(moveMotorClassInfo());
    obj::globalClassDB().registerClass(boxColliderClassInfo());
    obj::globalClassDB().registerClass(counterClassInfo());
    obj::globalClassDB().registerClass(sunSweepClassInfo());
    registered = true;
  }

} // namespace yr::gameplay