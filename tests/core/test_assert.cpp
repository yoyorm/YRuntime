#include <catch2/catch_test_macros.hpp>
#include <string>
#include <yr/core/assert.h>

using namespace yr::core;
#if YR_ENABLE_ASSERTS

namespace {
  yr::core::AssertInfo g_last{};
  int g_count = 0;

  yr::core::AssertAction recordingHandler(const yr::core::AssertInfo& info) noexcept {
    g_last = info;
    ++g_count;
    return yr::core::AssertAction::kContinue; // 关键：不停，测试才能继续
  }
} // namespace

TEST_CASE("assert: 条件成立时无副作用", "[core][assert]") {
  YR_ASSERT(true);
  YR_ASSERT(1 + 1 == 2);
  SUCCEED(); // 走到这里就算通过
}

TEST_CASE("assert: 条件只求值一次", "[core][assert]") {
  int n = 0;
  YR_ASSERT(++n == 1);
  REQUIRE(n == 1); // 如果是 2，说明宏里求值了两次
}

TEST_CASE("assert: 可以安全用在 if-else 里", "[core][assert]") {
  bool elseRan = false;
  if (false)
    YR_ASSERT(false); // 不执行，但 else 不能吃掉它
  else
    elseRan = true;
  REQUIRE(elseRan);
}

TEST_CASE("assert: 失败时报告正确的位置信息", "[core][assert]") {
  yr::core::setAssertHandler(recordingHandler);
  g_count = 0;

  const int expectedLine = __LINE__ + 1;
  YR_ASSERT(false);

  CHECK(g_count == 1);
  CHECK(g_last.line == expectedLine);
  CHECK(std::string(g_last.expr) == "false");
  CHECK(std::string(g_last.file).find("test_assert.cpp") != std::string::npos);

  yr::core::setAssertHandler(nullptr); // 恢复默认，别忘了
}

TEST_CASE("assert: handler 返回 kContinue 时程序继续执行", "[core][assert]") {
  yr::core::setAssertHandler(recordingHandler);
  bool reached = false;
  YR_ASSERT(1 == 2);
  reached = true; // 能走到这说明没终止
  CHECK(reached);
  yr::core::setAssertHandler(nullptr);
}

#else
// ── 断言关闭时：反向验证"确实被抹掉了" ──
TEST_CASE("assert: release 下条件完全不被求值", "[core][assert]") {
  int n = 0;
  YR_ASSERT(++n == 1);
  REQUIRE(n == 0); // 证明 ++n 从未执行
}

#endif
