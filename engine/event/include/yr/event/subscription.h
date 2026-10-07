#pragma once

#include <cstdint>
#include <typeindex>

namespace yr::evt {

  class EventBus;

  // 订阅句柄（RAII）：析构或 reset() 时自动退订。
  // 持有 EventBus*（非拥有）+ 事件类型 + 订阅 id；正常使用要求本句柄不晚于其
  // EventBus 销毁。避免全局/静态 Subscription，必要时在退出前显式 reset()。
  class Subscription {
  public:
    Subscription() noexcept = default;
    Subscription(EventBus* eventBus, std::type_index type, std::uint32_t id) noexcept
        : eventBus_(eventBus), type_(type), id_(id) {}
    ~Subscription();

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;

    // 主动退订并释放句柄：有效时先让 EventBus 取消订阅，再清空 id_/总线指针等
    void reset() noexcept;

    // 仅在持有总线指针且 id_ != 0 时为 true；
    [[nodiscard]] bool valid() const noexcept;

  private:
    friend class EventBus;
    EventBus* eventBus_ = nullptr;
    std::type_index type_{typeid(void)};
    std::uint32_t id_ = 0;
  };

} // namespace yr::evt
