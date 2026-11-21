#include "yr/input/input_router.h"
#include <utility>
namespace yr::input {

  void InputRouter::addSource(std::unique_ptr<IInputSource> source) {
    sources_.push_back(std::move(source));
  }

  void InputRouter::pollSources() {
    for (auto& source : sources_) {
      source->poll(*this);
    }
  }

  void InputRouter::clear() {
    events_.clear();
  }

  void InputRouter::submit(InputEvent event) {
    events_.push_back(std::move(event));
  }

  void InputRouter::beginFrame() {
    clear();
    pollSources();
  }

  [[nodiscard]] std::span<const InputEvent> InputRouter::events() const noexcept {
    return events_;
  }

} // namespace yr::input
