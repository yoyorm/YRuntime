#include "yr/input/input_system.h"

#include <utility>

#include <yr/event/event_bus.h>

namespace yr::input {

  void InputSystem::addSource(std::unique_ptr<IInputSource> source) {
    router_.addSource(std::move(source));
  }

  void InputSystem::beginFrame() {
    router_.beginFrame();
    state_.beginFrame();

    for (const InputEvent& event : router_.events()) {
      state_.submit(event);
      if (bus_ != nullptr) {
        bus_->post(event);
      }
    }
  }

} // namespace yr::input