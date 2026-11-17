#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <yr/job/job_system.h>

namespace {

  using yr::job::JobSystem;
  using yr::job::TaskHandle;

} // namespace

// 1 个 worker 被 blocker 占住；target 还在队列里。
// 主线程调 target.wait() 时应主动从队列取出 target 执行并立即返回，不必等 blocker 结束。
TEST_CASE("handle_wait_help: wait executes queued target task on the main thread", "[job][handle_wait_help]") {
  JobSystem jobs(1);

  std::atomic<bool> blocker_started{false};
  std::atomic<bool> release_blocker{false};
  std::atomic<bool> target_done{false};
  std::atomic<bool> target_ran_on_main{false};
  const std::thread::id mainThread = std::this_thread::get_id();

  const TaskHandle blocker = jobs.submit([&] {
    blocker_started.store(true, std::memory_order_release);
    while (!release_blocker.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  });

  while (!blocker_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  const TaskHandle target = jobs.submit([&] {
    if (std::this_thread::get_id() == mainThread) {
      target_ran_on_main.store(true, std::memory_order_release);
    }
    target_done.store(true, std::memory_order_release);
  });

  std::thread watchdog([&] {
    for (int tick = 0; tick < 50; ++tick) {
      if (target_done.load(std::memory_order_acquire)) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    release_blocker.store(true, std::memory_order_release);
  });

  target.wait();
  const bool blocker_still_running = !blocker.ready();

  release_blocker.store(true, std::memory_order_release);
  watchdog.join();
  jobs.waitAll();

  CHECK(target.ready());
  CHECK(target_ran_on_main.load(std::memory_order_acquire));
  CHECK(blocker_still_running);
}

// 1 个 worker 被 blocker 占住且依赖队列里的 releaser；
// 主线程调 blocker.wait() 时帮忙执行 releaser，从而解开 blocker。
TEST_CASE("handle_wait_help: wait executes queued helper task to unblock its own task", "[job][handle_wait_help]") {
  JobSystem jobs(1);

  std::atomic<bool> blocker_started{false};
  std::atomic<bool> release{false};
  std::atomic<bool> helper_ran_on_main{false};
  const std::thread::id mainThread = std::this_thread::get_id();

  const TaskHandle blocker = jobs.submit([&] {
    blocker_started.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  });

  while (!blocker_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  (void)jobs.submit([&] {
    if (std::this_thread::get_id() == mainThread) {
      helper_ran_on_main.store(true, std::memory_order_release);
    }
    release.store(true, std::memory_order_release);
  });

  std::thread watchdog([&] {
    for (int tick = 0; tick < 50; ++tick) {
      if (release.load(std::memory_order_acquire)) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    release.store(true, std::memory_order_release);
  });

  blocker.wait();
  watchdog.join();
  jobs.waitAll();

  CHECK(blocker.ready());
  CHECK(helper_ran_on_main.load(std::memory_order_acquire));
}

// 主线程在 blocker.wait() 中睡下后，另一线程才提交 releaser；
// 验证 submit 能唤醒正在 TaskHandle::wait() 中等待的主线程并由其帮忙执行。
TEST_CASE("handle_wait_help: wait executes tasks submitted during the wait", "[job][handle_wait_help]") {
  JobSystem jobs(1);

  std::atomic<bool> blocker_started{false};
  std::atomic<bool> release{false};
  std::atomic<bool> releaser_ran_on_main{false};
  const std::thread::id mainThread = std::this_thread::get_id();

  const TaskHandle blocker = jobs.submit([&] {
    blocker_started.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  });

  while (!blocker_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  std::thread submitter([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    (void)jobs.submit([&] {
      if (std::this_thread::get_id() == mainThread) {
        releaser_ran_on_main.store(true, std::memory_order_release);
      }
      release.store(true, std::memory_order_release);
    });
  });

  std::atomic<bool> rescued{false};
  std::thread watchdog([&] {
    for (int tick = 0; tick < 50; ++tick) {
      if (release.load(std::memory_order_acquire)) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    rescued.store(true, std::memory_order_release);
    release.store(true, std::memory_order_release);
  });

  blocker.wait();
  submitter.join();
  watchdog.join();
  jobs.waitAll();

  CHECK_FALSE(rescued.load(std::memory_order_acquire));
  CHECK(blocker.ready());
  CHECK(releaser_ran_on_main.load(std::memory_order_acquire));
}

// 2 个 worker：worker 1 跑慢任务，worker 2 跑快任务；
// fast.wait() 必须在 fast 完成时立即返回，不能退化成等所有 pending 归零。
TEST_CASE("handle_wait_help: wait returns when its own task finishes without waiting for slow tasks",
          "[job][handle_wait_help]") {
  JobSystem jobs(2);

  std::atomic<bool> slow_started{false};
  std::atomic<bool> allow_slow_finish{false};
  std::atomic<bool> fast_returned{false};
  std::atomic<bool> rescued{false};

  const TaskHandle slow = jobs.submit([&] {
    slow_started.store(true, std::memory_order_release);
    while (!allow_slow_finish.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  });

  while (!slow_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  const TaskHandle fast = jobs.submit([] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); });

  std::thread watchdog([&] {
    for (int tick = 0; tick < 50; ++tick) {
      if (fast_returned.load(std::memory_order_acquire)) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    rescued.store(true, std::memory_order_release);
    allow_slow_finish.store(true, std::memory_order_release);
  });

  fast.wait();
  fast_returned.store(true, std::memory_order_release);
  const bool slow_was_still_running = !slow.ready();

  allow_slow_finish.store(true, std::memory_order_release);
  watchdog.join();
  slow.wait();

  CHECK_FALSE(rescued.load(std::memory_order_acquire));
  CHECK(fast.ready());
  CHECK(slow_was_still_running);
  CHECK(slow.ready());
}
