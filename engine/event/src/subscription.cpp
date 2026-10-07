#include <typeindex>
#include <yr/event/event_bus.h>
#include <yr/event/subscription.h>

namespace yr::evt {

  Subscription::~Subscription() {
    reset();
  }

  Subscription::Subscription(Subscription&& other) noexcept
      : eventBus_(other.eventBus_), type_(other.type_), id_(other.id_) {
    // 把源句柄置为无效，防止重复退订。
    other.eventBus_ = nullptr;
    other.type_ = std::type_index(typeid(void));
    other.id_ = 0;
  }

  Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
      reset();
      eventBus_ = other.eventBus_;
      type_ = other.type_;
      id_ = other.id_;
      other.eventBus_ = nullptr;
      other.type_ = std::type_index(typeid(void));
      other.id_ = 0;
    }
    return *this;
  }

  void Subscription::reset() noexcept {
    // 先取消订阅然后重置
    if (valid()) {
      eventBus_->unsubscribe(*this);
    }
    eventBus_ = nullptr;
    type_ = std::type_index(typeid(void));
    id_ = 0;
  }

  bool Subscription::valid() const noexcept {
    return eventBus_ != nullptr && id_ != 0;
  }

} // namespace yr::evt