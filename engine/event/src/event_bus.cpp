#include <typeindex>
#include <yr/core/assert.h>
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
          entry.active = false; // 只标记，不立刻删除；物理清理见 cleanupInactive()
          break;
        }
      }
    }
    sub.eventBus_ = nullptr;
    sub.type_ = std::type_index(typeid(void));
    sub.id_ = 0;
  }

  void EventBus::cleanupInactive() {
    // 到达这里说明 publishDepth_ 已归零，栈上无 publish 正在遍历，可安全 erase。
    // 暂不追踪 dirty type：扫描全部类型，空分表保留。
    for (auto& table : subscribers_) {
      std::erase_if(table.second, [](const Entry& entry) { return !entry.active; });
    }
  }

  void EventBus::flush() {
    YR_ASSERT_MSG(!flushing_, "EventBus::flush: recursive flush is not allowed");
    if (flushing_) {
      return;
    }

    // RAII 恢复 flushing_
    struct FlushGuard {
      bool& flag;
      explicit FlushGuard(bool& value) noexcept : flag(value) { flag = true; }
      ~FlushGuard() { flag = false; }
    };
    FlushGuard guard(flushing_);

    auto& queueToDrain = writeQueueIs0_ ? pendings0_ : pendings1_;
    writeQueueIs0_ = !writeQueueIs0_; // 切换等待队列

    while (!queueToDrain.empty()) {
      std::unique_ptr<PendingEvent> pendingEvent = std::move(queueToDrain.front());
      queueToDrain.pop_front(); // 先弹出再派发：即使 dispatch 抛异常也不会重复派发。

      pendingEvent->dispatch(*this);
    }
    (void)guard;
  }

  EventBus& globalEventBus() noexcept {
    static EventBus database;
    return database;
  }

} // namespace yr::evt