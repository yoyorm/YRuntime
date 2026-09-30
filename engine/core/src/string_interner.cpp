#include <yr/core/string_interner.h>

namespace yr::core {

  StringId StringInterner::intern(std::string_view text) {
    const auto found = dictionary_.find(text);
    if (found != dictionary_.end()) {
      return StringId(found->second);
    }

    storage_.emplace_back(std::string(text));
    const StringNode* node = &storage_.back();
    dictionary_.emplace(node->value, node);
    return StringId(node);
  }

  std::string_view StringInterner::lookup(StringId id) const noexcept {
    return id.view();
  }

  std::size_t StringInterner::size() const noexcept {
    return dictionary_.size();
  }

  std::string_view StringId::view() const noexcept {
    return node_ == nullptr ? std::string_view{} : node_->value;
  }

  StringInterner& globalStringInterner() noexcept {
    static StringInterner interner;
    return interner;
  }

} // namespace yr::core
