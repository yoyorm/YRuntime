#include <catch2/catch_test_macros.hpp>

#include <yr/core/log.h>

#include <string>

#if YR_ENABLE_LOG

// ============================================================================
// 日志开启（所有 preset）
// ============================================================================

namespace {
  yr::core::LogInfo g_last{};
  std::string g_lastMessage; // message 是 view，存一份拷贝
  int g_count = 0;

  void recordingHandler(const yr::core::LogInfo& info) noexcept {
    g_last = info;
    g_lastMessage.assign(info.message.data(), info.message.size());
    ++g_count;
  }

  void beginCapture() {
    g_last = yr::core::LogInfo{};
    g_lastMessage.clear();
    g_count = 0;
    yr::core::setMinLogLevel(yr::core::LogLevel::kDebug); // 测内容时先全放行
    yr::core::setLogHandler(recordingHandler);
  }

  void endCapture() {
    yr::core::setLogHandler(nullptr);
    yr::core::setMinLogLevel(yr::core::defaultMinLogLevel());
  }
} // namespace

TEST_CASE("log: handler 收到完整字段", "[core][log]") {
  beginCapture();

  const int expectedLine = __LINE__ + 1; // 不要硬编码行号
  YR_LOG_WARN("scene", "node entered tree");

  CHECK(g_count == 1);
  CHECK(g_last.level == yr::core::LogLevel::kWarn);
  REQUIRE(g_last.tag != nullptr);
  CHECK(std::string(g_last.tag) == "scene");
  CHECK(g_last.line == expectedLine);
  REQUIRE(g_last.file != nullptr);
  CHECK(std::string(g_last.file).find("test_log.cpp") != std::string::npos);
  REQUIRE(g_last.func != nullptr);
  CHECK(std::string(g_last.func).size() > 0);
  CHECK(g_lastMessage == "node entered tree");

  endCapture();
}

TEST_CASE("log: 低于阈值的消息不进 handler", "[core][log]") {
  beginCapture();
  yr::core::setMinLogLevel(yr::core::LogLevel::kWarn);

  YR_LOG_INFO("t", "应被丢弃");
  CHECK(g_count == 0);

  YR_LOG_WARN("t", "应通过");
  CHECK(g_count == 1);

  YR_LOG_ERROR("t", "应通过");
  CHECK(g_count == 2);

  endCapture();
}

TEST_CASE("log: 连续多条逐条到达 handler", "[core][log]") {
  beginCapture();

  YR_LOG_INFO("a", "1");
  YR_LOG_INFO("b", "2");
  YR_LOG_INFO("c", "3");

  CHECK(g_count == 3);
  CHECK(std::string(g_last.tag) == "c"); // g_last 保留最后一条
  CHECK(g_lastMessage == "3");

  endCapture();
}

TEST_CASE("log: 默认阈值与编译期开关一致", "[core][log]") {
#if YR_ENABLE_DEBUG_LOG
  CHECK(yr::core::defaultMinLogLevel() == yr::core::LogLevel::kDebug);
#else
  CHECK(yr::core::defaultMinLogLevel() == yr::core::LogLevel::kInfo);
#endif
}

#if YR_ENABLE_DEBUG_LOG

TEST_CASE("log: debug 构建下 YR_LOG_DEBUG 能到达 handler", "[core][log]") {
  beginCapture();

  YR_LOG_DEBUG("scene", "帧级细节");

  CHECK(g_count == 1);
  CHECK(g_last.level == yr::core::LogLevel::kDebug);
  CHECK(g_lastMessage == "帧级细节");

  endCapture();
}

#else

TEST_CASE("log(off): release 下 YR_LOG_DEBUG 不产生记录", "[core][log]") {
  beginCapture();

  YR_LOG_DEBUG("scene", "不该出现");

  CHECK(g_count == 0);

  endCapture();
}

#endif // YR_ENABLE_DEBUG_LOG

TEST_CASE("log: [WIP] message 目前就是格式串本身", "[core][log][wip]") {
  beginCapture();

  YR_LOG_INFO("t", "x={}");

  // 格式化落地后本用例必须改写
  CHECK(g_lastMessage == "x={}");

  endCapture();
}

TEST_CASE("log: 所有宏都能编译并到达 handler", "[core][log]") {
  beginCapture();

  YR_LOG_DEBUG("t", "d"); // release 下编译期消失，不计入
  YR_LOG_INFO("t", "i");
  YR_LOG_WARN("t", "w");
  YR_LOG_ERROR("t", "e");
  YR_LOG_FATAL("t", "f");

#if YR_ENABLE_DEBUG_LOG
  CHECK(g_count == 5);
#else
  CHECK(g_count == 4);
#endif

  endCapture();
}

TEST_CASE("log: defaultLogHandler 可直接调用", "[core][log]") {
  const yr::core::LogInfo info{yr::core::LogLevel::kInfo, "unit_test", "test_log.cpp", 1, "someFunc", "hello"};
  yr::core::defaultLogHandler(info); // 会往 stderr 打一行，这是预期的
  SUCCEED();
}

TEST_CASE("log: setLogHandler(nullptr) 恢复默认后仍可用", "[core][log]") {
  yr::core::setLogHandler(nullptr);
  YR_LOG_INFO("t", "这一行会打到 stderr（预期）");
  SUCCEED();
}

#else // YR_ENABLE_LOG == 0

// ============================================================================
// 日志整体关闭（当前无 preset 使用，留着保证该配置可编译）
// ============================================================================

TEST_CASE("log(off): 所有宏都编译期消失", "[core][log][off]") {
  YR_LOG_DEBUG("t", "x");
  YR_LOG_INFO("t", "x");
  YR_LOG_WARN("t", "x");
  YR_LOG_ERROR("t", "x");
  YR_LOG_FATAL("t", "x");
  SUCCEED();
}

#endif
