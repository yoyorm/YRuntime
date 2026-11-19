#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include <yr/core/thread_check.h>

namespace {

  using yr::core::isMainThread;
  using yr::core::mainThreadMarked;
  using yr::core::markMainThread;
  using yr::core::resetMainThreadForTesting;

} // namespace

TEST_CASE("thread_check: unmarked state reports not-main and not-marked", "[core][thread_check]") {
  resetMainThreadForTesting();

  CHECK_FALSE(mainThreadMarked());
  CHECK_FALSE(isMainThread());
}

TEST_CASE("thread_check: marking makes the current thread main", "[core][thread_check]") {
  resetMainThreadForTesting();

  markMainThread();
  CHECK(mainThreadMarked());
  CHECK(isMainThread());

  std::atomic<bool> worker_sees_main{true};
  std::thread worker([&] { worker_sees_main.store(isMainThread(), std::memory_order_relaxed); });
  worker.join();

  CHECK_FALSE(worker_sees_main.load(std::memory_order_relaxed));
}

TEST_CASE("thread_check: reset clears the mark", "[core][thread_check]") {
  markMainThread();
  REQUIRE(isMainThread());

  resetMainThreadForTesting();

  CHECK_FALSE(mainThreadMarked());
  CHECK_FALSE(isMainThread());
}

TEST_CASE("thread_check: marking twice from the same thread is idempotent", "[core][thread_check]") {
  resetMainThreadForTesting();

  markMainThread();
  markMainThread();

  CHECK(isMainThread());
}

#if YR_ENABLE_ASSERTS
namespace {
  std::atomic<int> g_assert_reports{0};
} // namespace

TEST_CASE("thread_check: assert macro is silent on main and reports on workers", "[core][thread_check]") {
  resetMainThreadForTesting();

  // 未标记：宏不做检查。
  YR_ASSERT_MAIN_THREAD();

  markMainThread();

  // 主线程：通过。
  YR_ASSERT_MAIN_THREAD();

  g_assert_reports.store(0);
  ::yr::core::setAssertHandler(+[](const ::yr::core::AssertInfo&) noexcept {
    g_assert_reports.fetch_add(1, std::memory_order_relaxed);
    return ::yr::core::AssertAction::kContinue;
  });

  std::thread worker([&] { YR_ASSERT_MAIN_THREAD(); });
  worker.join();

  ::yr::core::setAssertHandler(nullptr);

  CHECK(g_assert_reports.load(std::memory_order_relaxed) == 1);
}
#endif