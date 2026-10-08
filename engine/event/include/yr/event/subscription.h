#pragma once

#include <cstdint>
#include <typeindex>

namespace yr::evt {

  class EventBus;

  // 订阅句柄（RAII）：析构或 reset() 时自动退订。
  // 非拥有其 EventBus：要求句柄不晚于 EventBus 销毁；避免全局/静态句柄，
  // 必要时在退出前显式 reset()。
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

    // 主动退订并释放句柄。
    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept;

  private:
    friend class EventBus;
    EventBus* eventBus_ = nullptr;
    std::type_index type_{typeid(void)};
    std::uint32_t id_ = 0;
  };

} // namespace yr::evt
