#include <cstdio>
#include <yr/core/assert.h>

namespace yr::core {
  namespace {
    AssertHandler g_handler = nullptr;
    bool g_handling = false; //? 正在处理的哨兵,防止递归处理
  } // namespace

  AssertAction defaultAssertHandler(const AssertInfo& info) noexcept {
    std::fprintf(stderr, "%s:%d: %s: 断言失败: %s\n", info.file, info.line, info.func, info.expr);
    return AssertAction::kAbort;
  }

  void setAssertHandler(AssertHandler handler) noexcept {
    g_handler = handler;
  }

  void reportAssert(const AssertInfo& info) noexcept {
    if (g_handling) {
      __builtin_trap();
    }
    g_handling = true;

    AssertHandler h = (g_handler != nullptr) ? g_handler : &defaultAssertHandler;
    const AssertAction action = h(info);

    g_handling = false;

    if (action == AssertAction::kAbort) {
      __builtin_trap();
    }
  }

} // namespace yr::core