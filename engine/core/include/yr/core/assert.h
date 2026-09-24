#pragma once

namespace yr::core {

  struct AssertInfo {
    const char* file;
    int line;
    const char* func;
    const char* expr; // 表达式的文本
  };

  enum class AssertAction : short { kAbort, kContinue };

  // 处理 AssertInfo 信息的 handler
  using AssertHandler = AssertAction (*)(const AssertInfo&) noexcept;

  // 默认 handler：打印到 stderr，返回 kAbort
  AssertAction defaultAssertHandler(const AssertInfo& info) noexcept;

  // 替换 handler。传 nullptr 恢复默认。
  void setAssertHandler(AssertHandler handler) noexcept;

  // 宏调用它：取当前 handler → 调用 → 若返回 kAbort 则 __builtin_trap()
  void reportAssert(const AssertInfo& info) noexcept;

} // namespace yr::core

#if !defined(YR_ENABLE_ASSERTS)
#ifdef NDEBUG
#define YR_ENABLE_ASSERTS 0
#else
#define YR_ENABLE_ASSERTS 1
#endif
#endif

// ... YR_ENABLE_ASSERTS 的默认值定义 ...

#if YR_ENABLE_ASSERTS

#define YR_ASSERT(cond)                                                                                                \
  do {                                                                                                                 \
    if ((cond)) {                                                                                                      \
      break;                                                                                                           \
    } else {                                                                                                           \
      yr::core::AssertInfo info{};                                                                                     \
      info.file = __FILE__;                                                                                            \
      info.line = __LINE__;                                                                                            \
      info.func = __func__;                                                                                            \
      info.expr = #cond;                                                                                               \
      yr::core::reportAssert(info);                                                                                    \
    }                                                                                                                  \
  } while (false)

#else
#define YR_ASSERT(cond) ((void)0)
#endif
