#include <catch2/catch_test_macros.hpp>

#include <yr/core/assert.h>

#include <string>

#if YR_ENABLE_ASSERTS

// ============================================================================
// 断言开启（debug / asan preset）
// ============================================================================

namespace {
  yr::core::AssertInfo g_last{};
  int g_count = 0;

  // 关键：返回 kContinue，否则断言一触发进程就死，什么都验证不了
  yr::core::AssertAction recordingHandler(const yr::core::AssertInfo& info) noexcept {
    g_last = info;
    ++g_count;
    return yr::core::AssertAction::kContinue;
  }

  void resetRecorder() {
    g_last = yr::core::AssertInfo{};
    g_count = 0;
    yr::core::setAssertHandler(recordingHandler);
  }
} // namespace

TEST_CASE("assert: 条件成立时无副作用", "[core][assert]") {
  resetRecorder();
  YR_ASSERT(true);
  YR_ASSERT(1 + 1 == 2);
  CHECK(g_count == 0); // 通过的断言不该惊动 handler
  yr::core::setAssertHandler(nullptr);
}

TEST_CASE("assert: 条件只求值一次", "[core][assert]") {
  int n = 0;
  YR_ASSERT(++n == 1);
  REQUIRE(n == 1); // 若为 2，说明宏体里求值了两次
}

TEST_CASE("assert: 可以安全用在 if-else 里", "[core][assert]") {
  bool elseRan = false;
  if (false)
    YR_ASSERT(false); // 不执行，但 else 不能被宏吃掉
  else
    elseRan = true;
  REQUIRE(elseRan);
}

TEST_CASE("assert: 失败时报告正确的位置信息", "[core][assert]") {
  resetRecorder();

  const int expectedLine = __LINE__ + 1; // 不要硬编码行号
  YR_ASSERT(false);

  CHECK(g_count == 1);
  CHECK(g_last.line == expectedLine);
  CHECK(std::string(g_last.expr) == "false");
  CHECK(g_last.file != nullptr);
  CHECK(std::string(g_last.file).find("test_assert.cpp") != std::string::npos);
  CHECK(g_last.func != nullptr);
  CHECK(std::char_traits<char>::length(g_last.func) > 0);

  yr::core::setAssertHandler(nullptr);
}

TEST_CASE("assert: handler 返回 kContinue 时程序继续执行", "[core][assert]") {
  resetRecorder();
  bool reached = false;
  YR_ASSERT(1 == 2);
  reached = true; // 能走到这里就说明没被终止
  CHECK(reached);
  yr::core::setAssertHandler(nullptr);
}

TEST_CASE("assert: 无消息版本的 message 为 nullptr", "[core][assert][msg]") {
  resetRecorder();
  YR_ASSERT(false);
  CHECK(g_last.message == nullptr);
  yr::core::setAssertHandler(nullptr);
}

TEST_CASE("assert: MSG 版本传递消息文本，且 expr 仍是条件", "[core][assert][msg]") {
  resetRecorder();
  YR_ASSERT_MSG(2 + 2 == 5, "算术坏了");

  REQUIRE(g_count == 1);
  CHECK(g_last.message != nullptr);
  CHECK(std::string(g_last.message) == "算术坏了");
  // expr 应该是条件的源码文本，不是消息
  CHECK(std::string(g_last.expr) == "2 + 2 == 5");

  yr::core::setAssertHandler(nullptr);
}

TEST_CASE("assert: 多次失败计数累加，g_last 保留最后一次", "[core][assert][msg]") {
  resetRecorder();
  YR_ASSERT(false);
  YR_ASSERT_MSG(false, "第二次");
  YR_ASSERT(1 == 2);

  CHECK(g_count == 3);
  // g_last 只保留最后一次，最后一次是 YR_ASSERT（无消息）
  CHECK(g_last.message == nullptr);
  CHECK(std::string(g_last.expr) == "1 == 2");

  yr::core::setAssertHandler(nullptr);
}

TEST_CASE("assert: MSG 版本可以用变量当消息", "[core][assert][msg]") {
  resetRecorder();
  const std::string dynamic = "运行时拼出来的消息";
  YR_ASSERT_MSG(false, dynamic.c_str());
  REQUIRE(g_last.message != nullptr);
  CHECK(std::string(g_last.message) == dynamic);
  yr::core::setAssertHandler(nullptr);
}

TEST_CASE("assert: 宏不污染调用方作用域", "[core][assert][hygiene]") {
  resetRecorder();

  // 故意声明与宏内部标识符同名的变量
  const char* msg = "外部消息";
  yr::core::AssertInfo info{};
  info.line = 999;

  YR_ASSERT_MSG(false, "内部消息");

  CHECK(info.line == 999);               // 外部的 info 没被宏改写
  CHECK(std::string(msg) == "外部消息"); // 外部的 msg 没被宏吃掉
  CHECK(g_last.line != 999);             // handler 收到的是宏自己的 info
  CHECK(std::string(g_last.message) == "内部消息");

  yr::core::setAssertHandler(nullptr);
}

TEST_CASE("verify: 成功时求值副作用，失败时走 handler", "[core][assert][verify]") {
  resetRecorder();

  int n = 0;
  YR_VERIFY(++n == 1);
  CHECK(n == 1);       // 副作用发生
  CHECK(g_count == 0); // 条件成立，不报告

  YR_VERIFY(n == 999);
  CHECK(g_count == 1); // 条件失败，报告了
  CHECK(std::string(g_last.expr) == "n == 999");

  yr::core::setAssertHandler(nullptr);
}

TEST_CASE("assert: 默认 handler 可直接调用（供自定义 handler 复用）", "[core][assert]") {
  const yr::core::AssertInfo info{"unit_test.cpp", 42, "someFunc", "a == b", nullptr};
  // 会往 stderr 打一行，这是预期的
  CHECK(yr::core::defaultAssertHandler(info) == yr::core::AssertAction::kAbort);
}

TEST_CASE("assert: setAssertHandler(nullptr) 恢复默认后仍可用", "[core][assert]") {
  yr::core::setAssertHandler(nullptr);
  YR_ASSERT(true); // 不该崩
  SUCCEED();
}

#else

// ============================================================================
// 断言关闭（release preset：-O3 -DNDEBUG）
// 这里验证的是"确实被抹掉了"，以及 YR_VERIFY 的例外语义
// ============================================================================

TEST_CASE("assert(off): YR_ASSERT 完全不求值", "[core][assert][off]") {
  int n = 0;
  YR_ASSERT(++n == 1);
  REQUIRE(n == 0); // 证明 ++n 从未执行
}

TEST_CASE("assert(off): YR_ASSERT_MSG 完全不求值", "[core][assert][off]") {
  int n = 0;
  YR_ASSERT_MSG(++n == 1, "不会被看到");
  REQUIRE(n == 0);
}

TEST_CASE("verify(off): YR_VERIFY 仍然求值 —— 这是它存在的唯一理由", "[core][assert][off][verify]") {
  int n = 0;
  YR_VERIFY(++n == 1);
  REQUIRE(n == 1); // 断言关了，副作用仍必须发生（file.close() 之类）

  // 失败时也不该中断进程
  bool survived = false;
  YR_VERIFY(1 == 2);
  survived = true;
  REQUIRE(survived);
}

TEST_CASE("assert(off): 所有宏都仍可编译", "[core][assert][off]") {
  // if(false) 包住 → 编译但不执行（YR_BREAKPOINT 真执行会终止进程）
  if (false) {
    YR_ASSERT(false);
    YR_ASSERT_MSG(false, "x");
    YR_VERIFY(false);
    YR_BREAKPOINT();
  }
  SUCCEED();
}

#endif
