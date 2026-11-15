#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <yr/job/job_system.h>

namespace {

  using yr::job::JobSystem;
  using yr::job::TaskHandle;

} // namespace

TEST_CASE("task_handle: submit returns a handle that becomes ready", "[job][task_handle]") {
  JobSystem jobs(2);
  std::atomic<bool> ran{false};

  const TaskHandle handle = jobs.submit([&] { ran.store(true, std::memory_order_release); });
  jobs.waitAll();

  CHECK(ran.load(std::memory_order_acquire));
  CHECK(handle.ready());
}

TEST_CASE("task_handle: wait blocks until the task completes", "[job][task_handle]") {
  JobSystem jobs(1);
  std::atomic<bool> ran{false};

  const TaskHandle handle = jobs.submit([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ran.store(true, std::memory_order_release);
  });

  CHECK_FALSE(handle.ready());
  CHECK_FALSE(ran.load(std::memory_order_acquire));

  handle.wait();

  CHECK(ran.load(std::memory_order_acquire));
  CHECK(handle.ready());
}

TEST_CASE("task_handle: copies share the same completion state", "[job][task_handle]") {
  JobSystem jobs(1);
  std::atomic<bool> ran{false};

  const TaskHandle handle = jobs.submit([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ran.store(true, std::memory_order_release);
  });
  const TaskHandle copy = handle;

  handle.wait();
  copy.wait();

  CHECK(ran.load(std::memory_order_acquire));
  CHECK(handle.ready());
  CHECK(copy.ready());
}

TEST_CASE("task_handle: zero workers complete inline", "[job][task_handle]") {
  JobSystem jobs(0);
  std::atomic<bool> ran{false};

  const TaskHandle handle = jobs.submit([&] { ran.store(true, std::memory_order_release); });

  CHECK(ran.load(std::memory_order_acquire));
  CHECK(handle.ready());
  handle.wait();
  CHECK(handle.ready());
}

TEST_CASE("task_handle: default handle is not ready and wait is a no-op", "[job][task_handle]") {
  TaskHandle handle;
  CHECK_FALSE(handle.ready());
  handle.wait();
  CHECK_FALSE(handle.ready());
}

TEST_CASE("task_handle: wait after completion returns immediately", "[job][task_handle]") {
  JobSystem jobs(2);
  std::atomic<bool> ran{false};

  const TaskHandle handle = jobs.submit([&] { ran.store(true, std::memory_order_release); });
  handle.wait();
  CHECK(ran.load(std::memory_order_acquire));
  CHECK(handle.ready());

  handle.wait();
  CHECK(handle.ready());
}
