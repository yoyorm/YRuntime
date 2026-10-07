#include <typeindex>
#include <yr/event/event_bus.h>

namespace yr::evt {

  void EventBus::unsubscribe(Subscription& sub) noexcept {
    if (sub.eventBus_ != this || sub.id_ == 0) {
      return;
    }
    const auto it = subscribers_.find(sub.type_);
    if (it != subscribers_.end()) {
      for (Entry& entry : it->second) {
        if (entry.id == sub.id_) {
          entry.active = false; // 只标记，不立刻删除
          break;
        }
      }
    }
    sub.eventBus_ = nullptr;
    sub.type_ = std::type_index(typeid(void));
    sub.id_ = 0;
  }

  EventBus& globalEventBus() noexcept {
    static EventBus database;
    return database;
  }

} // namespace yr::evt