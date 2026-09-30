#pragma once

#include <cstddef>
#include <list>
#include <string_view>
#include <unordered_map>

#include <yr/core/string_id.h>

namespace yr::core {

  // 字符串驻留表：将相同内容映射到同一个、地址稳定的字符串节点。
  // 第一版只追加、不删除；StringId 只在当前进程地址空间内有效。
  class StringInterner {
  public:
    ~StringInterner() = default;

    StringInterner(const StringInterner&) = delete;
    StringInterner& operator=(const StringInterner&) = delete;
    StringInterner(StringInterner&&) = delete;
    StringInterner& operator=(StringInterner&&) = delete;

    // 相同字符串返回相同 ID；新字符串分配新的有效 ID。
    [[nodiscard]] StringId intern(std::string_view text);

    // 无效 ID 返回空 view；有效 ID 返回其驻留字符串。
    [[nodiscard]] std::string_view lookup(StringId id) const noexcept;

    // 返回当前驻留表中的字符串数量。
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    StringInterner() noexcept = default;

    friend StringInterner& globalStringInterner() noexcept;

    using Storage = std::list<StringNode>;
    using Dictionary = std::unordered_map<std::string_view, const StringNode*>;

    Storage storage_;
    Dictionary dictionary_;
  };

  // 默认的进程级驻留表。StringId 的公共使用路径通过它共享同一 ID 空间。
  [[nodiscard]] StringInterner& globalStringInterner() noexcept;

} // namespace yr::core
