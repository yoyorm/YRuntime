#pragma once
// #include "yr/core/string_id.h"
#include "yr/input/input_event.h"
#include "yr/input/input_source.h"
#include <memory>
#include <span>
#include <vector>
namespace yr::input {
  // class InputEventState {
  // public:
  //   bool isPressed(yr::core::StringId action) const noexcept;
  //   bool isHeld(yr::core::StringId action) const noexcept;
  //   bool isReleased(yr::core::StringId action) const noexcept;
  //   float actionValue(yr::core::StringId action) const noexcept;
  // };

  class InputRouter final : public IInputSink {
  public:
    InputRouter() = default;
    ~InputRouter() = default;

    InputRouter(const InputRouter&) = delete;
    InputRouter& operator=(const InputRouter&) = delete;
    InputRouter(InputRouter&&) = delete;
    InputRouter& operator=(InputRouter&&) = delete;

    void addSource(std::unique_ptr<IInputSource> source);
    void submit(InputEvent event) override;
    void pollSources();
    void clear();
    [[nodiscard]] std::span<const InputEvent> events() const noexcept;
    void beginFrame();

  private:
    std::vector<std::unique_ptr<IInputSource>> sources_;
    std::vector<InputEvent> events_;
  };
} // namespace yr::input
