#pragma once

#include <string_view>

#include <yr/core/string_id.h>
#include <yr/input/input_event.h>
#include <yr/scene/node.h>

namespace yr::gameplay {

  // 单键边沿触发器：满足 phase 时发 triggered。
  // phase 可为 pressed / released / repeat / held（held 每帧发）。
  class KeyTrigger final : public scene::Node {
  public:
    void onUpdate(float deltaTime) override;

    [[nodiscard]] core::StringId key() const noexcept { return key_; }
    void setKey(std::string_view key);
    [[nodiscard]] input::KeyPhase phase() const noexcept { return phase_; }
    // 未知 phase 返回 false，保持原值（让 .scn 校验能报出来）。
    [[nodiscard]] bool setPhase(std::string_view phase);

    [[nodiscard]] const obj::ClassInfo* classInfo() const noexcept override;

  private:
    core::StringId key_{};
    input::KeyPhase phase_ = input::KeyPhase::kPressed;
  };

  [[nodiscard]] const obj::ClassInfo& keyTriggerClassInfo() noexcept;

} // namespace yr::gameplay