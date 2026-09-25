#pragma once

namespace yr::core {

  struct AssertInfo {
    const char* file;
    int line;
    const char* func;
    const char* expr; // 表达式的文本
    const char* message;
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

#if YR_ENABLE_ASSERTS

#define YR_ASSERT(cond)                                                                                                \
  do {                                                                                                                 \
    if ((cond)) {                                                                                                      \
      break;                                                                                                           \
    } else {                                                                                                           \
      ::yr::core::reportAssert({__FILE__, __LINE__, __func__, #cond, nullptr});                                        \
    }                                                                                                                  \
  } while (false)

#define YR_ASSERT_MSG(cond, msg)                                                                                       \
  do {                                                                                                                 \
    if ((cond)) {                                                                                                      \
      break;                                                                                                           \
    } else {                                                                                                           \
      ::yr::core::reportAssert({__FILE__, __LINE__, __func__, #cond, (msg)});                                          \
    }                                                                                                                  \
  } while (false)

#define YR_VERIFY(cond) YR_ASSERT(cond)

#else
#define YR_ASSERT(cond) ((void)0)
#define YR_ASSERT_MSG(cond, message) ((void)0)
#define YR_VERIFY(cond) ((void)!(cond)) // 只求值，不报告
#endif

#define YR_BREAKPOINT() __builtin_trap()
