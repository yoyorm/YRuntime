#pragma once

#include <memory>

#include <yr/input/input_router.h>
#include <yr/input/input_state.h>

namespace yr::evt {
  class EventBus;
}

namespace yr::input {

  // 输入组合根：拥有 Router 与 InputState，负责把本帧事件同时送给状态汇总与 EventBus。
  class InputSystem {
  public:
    InputSystem() = default;
    ~InputSystem() = default;

    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;
    InputSystem(InputSystem&&) = delete;
    InputSystem& operator=(InputSystem&&) = delete;

    void addSource(std::unique_ptr<IInputSource> source);

    // 非拥有；nullptr 时只更新 InputState，不向总线发事件。
    void setEventBus(evt::EventBus* bus) noexcept { bus_ = bus; }

    // 每帧阶段 1 调用一次：清空上一帧、泵 source、更新 state、投递原始事件。
    void beginFrame();

    [[nodiscard]] const InputState& state() const noexcept { return state_; }

    [[nodiscard]] const InputRouter& router() const noexcept { return router_; }

  private:
    InputRouter router_;
    InputState state_;
    evt::EventBus* bus_ = nullptr;
  };

} // namespace yr::input