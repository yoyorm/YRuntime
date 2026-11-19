#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <yr/job/job_system.h>

namespace {

  using yr::job::JobSystem;

} // namespace

// 基本计数：每个 submit 计一次提交，每个任务体执行完计一次完成。
TEST_CASE("job_stats: counts submitted and completed tasks", "[job][job_stats]") {
  constexpr std::size_t kTasks = 200;
  JobSystem jobs(4);

  std::atomic<std::size_t> ran{0};
  for (std::size_t index = 0; index < kTasks; ++index) {
    (void)jobs.submit([&] { ran.fetch_add(1, std::memory_order_relaxed); });
  }
  jobs.waitAll();

  const yr::job::JobStats stats = jobs.stats();

  CHECK(stats.submitted == kTasks);
  CHECK(stats.completed == kTasks);
  CHECK(stats.readyDepth == 0);
  CHECK(stats.helperCompleted <= stats.completed);
  CHECK(ran.load(std::memory_order_relaxed) == kTasks);
}

// workerCount == 0：调用线程内联执行，完成数应全部记在 helperCompleted。
TEST_CASE("job_stats: zero workers count inline completions as helper", "[job][job_stats]") {
  constexpr std::size_t kTasks = 16;
  JobSystem jobs(0);

  for (std::size_t index = 0; index < kTasks; ++index) {
    (void)jobs.submit([] {});
  }

  const yr::job::JobStats stats = jobs.stats();

  CHECK(stats.submitted == kTasks);
  CHECK(stats.completed == kTasks);
  CHECK(stats.helperCompleted == kTasks);
}

// 主线程帮忙执行的任务计入 helperCompleted。
TEST_CASE("job_stats: helper completions are counted when the main thread waits", "[job][job_stats]") {
  JobSystem jobs(1);

  std::atomic<bool> blocker_started{false};
  std::atomic<bool> release{false};

  (void)jobs.submit([&] {
    blocker_started.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  });

  while (!blocker_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  (void)jobs.submit([&] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); });

  std::thread watchdog([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    release.store(true, std::memory_order_release);
  });

  jobs.waitAll();
  watchdog.join();

  const yr::job::JobStats stats = jobs.stats();

  CHECK(stats.submitted == 2);
  CHECK(stats.completed == 2);
  CHECK(stats.helperCompleted >= 1);
}

// 峰值深度：worker 被占住时提交 5 个短任务，队列应出现 5 的深度峰值。
TEST_CASE("job_stats: peak ready depth reflects queued backlog", "[job][job_stats]") {
  JobSystem jobs(1);

  std::atomic<bool> blocker_started{false};
  std::atomic<bool> release{false};

  (void)jobs.submit([&] {
    blocker_started.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  });

  while (!blocker_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  for (int index = 0; index < 5; ++index) {
    (void)jobs.submit([] {});
  }

  const yr::job::JobStats backlog = jobs.stats();
  CHECK(backlog.peakReadyDepth >= 5);
  CHECK(backlog.readyDepth >= 5);

  release.store(true, std::memory_order_release);

  std::thread watchdog([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    release.store(true, std::memory_order_release);
  });

  jobs.waitAll();
  watchdog.join();

  const yr::job::JobStats drained = jobs.stats();
  CHECK(drained.submitted == 6);
  CHECK(drained.completed == 6);
  CHECK(drained.readyDepth == 0);
}

// 依赖链：三个任务都只计一次提交、一次完成。
TEST_CASE("job_stats: dependency chains are counted once per task", "[job][job_stats]") {
  JobSystem jobs(2);

  int value = 0;
  const auto a = jobs.submit([&] { value += 1; });
  const auto b = jobs.submit([&] { value += 2; }, {a});
  const auto c = jobs.submit([&] { value += 4; }, {b});

  c.wait();
  jobs.waitAll();

  const yr::job::JobStats stats = jobs.stats();

  CHECK(stats.submitted == 3);
  CHECK(stats.completed == 3);
  CHECK(value == 7);
}

// 两个 JobSystem 的计数互不影响。
TEST_CASE("job_stats: two job systems keep independent counters", "[job][job_stats]") {
  yr::job::JobStats firstStats;

  {
    JobSystem first(2);
    for (int index = 0; index < 7; ++index) {
      (void)first.submit([] {});
    }
    first.waitAll();
    firstStats = first.stats();
  }

  JobSystem second(2);
  for (int index = 0; index < 3; ++index) {
    (void)second.submit([] {});
  }
  second.waitAll();

  const yr::job::JobStats secondStats = second.stats();

  CHECK(firstStats.submitted == 7);
  CHECK(firstStats.completed == 7);
  CHECK(secondStats.submitted == 3);
  CHECK(secondStats.completed == 3);
}