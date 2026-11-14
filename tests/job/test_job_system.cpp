#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

#include <yr/job/job_system.h>

namespace {

  using yr::job::JobSystem;

} // namespace

TEST_CASE("job_system: zero workers execute inline", "[job][job_system]") {
  JobSystem jobs(0);
  CHECK(jobs.workerCount() == 0);

  std::atomic<int> count{0};
  jobs.submit([&] { count.fetch_add(1, std::memory_order_relaxed); });

  CHECK(count.load(std::memory_order_relaxed) == 1);
  jobs.waitAll();
  CHECK(count.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("job_system: runs submitted tasks across workers", "[job][job_system]") {
  constexpr int kTasks = 200;
  JobSystem jobs(4);
  CHECK(jobs.workerCount() == 4);

  std::atomic<int> count{0};
  for (int index = 0; index < kTasks; ++index) {
    jobs.submit([&] { count.fetch_add(1, std::memory_order_relaxed); });
  }

  jobs.waitAll();
  CHECK(count.load(std::memory_order_relaxed) == kTasks);
}

TEST_CASE("job_system: waitAll covers nested submissions", "[job][job_system]") {
  JobSystem jobs(2);
  std::atomic<int> count{0};

  jobs.submit([&] {
    count.fetch_add(1, std::memory_order_relaxed);
    jobs.submit([&] { count.fetch_add(1, std::memory_order_relaxed); });
  });

  jobs.waitAll();
  CHECK(count.load(std::memory_order_relaxed) == 2);
}

TEST_CASE("job_system: destructor drains submitted tasks", "[job][job_system]") {
  constexpr int kTasks = 32;
  std::atomic<int> count{0};

  {
    JobSystem jobs(2);
    for (int index = 0; index < kTasks; ++index) {
      jobs.submit([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        count.fetch_add(1, std::memory_order_relaxed);
      });
    }
  }

  CHECK(count.load(std::memory_order_relaxed) == kTasks);
}

TEST_CASE("job_system: empty waitAll returns", "[job][job_system]") {
  JobSystem jobs(2);
  CHECK(jobs.workerCount() == 2);
  jobs.waitAll();
  jobs.waitAll();
}

TEST_CASE("job_system: waitAll is idempotent", "[job][job_system]") {
  constexpr int kTasks = 16;
  JobSystem jobs(3);
  std::atomic<int> count{0};

  for (int index = 0; index < kTasks; ++index) {
    jobs.submit([&] { count.fetch_add(1, std::memory_order_relaxed); });
  }

  jobs.waitAll();
  CHECK(count.load(std::memory_order_relaxed) == kTasks);
  jobs.waitAll();
  CHECK(count.load(std::memory_order_relaxed) == kTasks);
}

TEST_CASE("job_system: workerCount reflects configuration", "[job][job_system]") {
  JobSystem zero(0);
  JobSystem one(1);
  JobSystem four(4);

  CHECK(zero.workerCount() == 0);
  CHECK(one.workerCount() == 1);
  CHECK(four.workerCount() == 4);
}
