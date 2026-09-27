#include <catch2/catch_test_macros.hpp>

#include <yr/core/timer.h>

#include <chrono>
#include <thread>
#include <type_traits>

static_assert(std::is_same_v<yr::core::Duration, std::chrono::nanoseconds>);
static_assert(std::is_same_v<yr::core::TimePoint, std::chrono::steady_clock::time_point>);
static_assert(!std::is_copy_constructible_v<yr::core::ScopeTimer>);
static_assert(!std::is_move_constructible_v<yr::core::ScopeTimer>);

TEST_CASE("timer: elapsed is non-negative", "[core][timer]") {
  const yr::core::ScopeTimer timer;

  CHECK(timer.elapsed() >= yr::core::Duration::zero());
}

TEST_CASE("timer: elapsed observes time passing", "[core][timer]") {
  const yr::core::ScopeTimer timer;
  std::this_thread::sleep_for(std::chrono::milliseconds(2));

  CHECK(timer.elapsed() >= std::chrono::milliseconds(1));
}

TEST_CASE("timer: restart returns elapsed time and starts a new interval", "[core][timer]") {
  yr::core::ScopeTimer timer;
  std::this_thread::sleep_for(std::chrono::milliseconds(2));

  const yr::core::Duration first = timer.restart();
  CHECK(first >= std::chrono::milliseconds(1));
  CHECK(timer.elapsed() >= yr::core::Duration::zero());

  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  CHECK(timer.elapsed() >= std::chrono::milliseconds(1));
}
