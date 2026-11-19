#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <random>
#include <span>
#include <thread>
#include <vector>

#include <yr/job/job_system.h>

namespace {

  using yr::job::JobSystem;
  using yr::job::TaskHandle;

} // namespace

// 单依赖 A -> B：B 只允许在 A 完成后才开始执行。
TEST_CASE("job_deps: dependent task runs only after prerequisite completes", "[job][job_deps]") {
  JobSystem jobs(2);

  std::atomic<bool> a_done{false};
  std::atomic<bool> b_saw_a_done{false};

  const TaskHandle a = jobs.submit([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    a_done.store(true, std::memory_order_release);
  });

  const TaskHandle b =
      jobs.submit([&] { b_saw_a_done.store(a_done.load(std::memory_order_acquire), std::memory_order_release); }, {a});

  b.wait();
  jobs.waitAll();

  CHECK(a.ready());
  CHECK(b.ready());
  CHECK(b_saw_a_done.load(std::memory_order_acquire));
}

// 菱形依赖 A -> {B, C} -> D：D 在 B 与 C 都完成后才执行。
TEST_CASE("job_deps: diamond dependency graph executes in topological order", "[job][job_deps]") {
  JobSystem jobs(3);

  std::atomic<int> value_a{0};
  std::atomic<int> value_b{0};
  std::atomic<int> value_c{0};
  std::atomic<int> value_d{0};

  const TaskHandle a = jobs.submit([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    value_a.store(7, std::memory_order_release);
  });

  const TaskHandle b = jobs.submit(
      [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        value_b.store(value_a.load(std::memory_order_acquire) * 2, std::memory_order_release);
      },
      {a});

  const TaskHandle c = jobs.submit(
      [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        value_c.store(value_a.load(std::memory_order_acquire) + 3, std::memory_order_release);
      },
      {a});

  const std::vector<TaskHandle> bc{b, c};
  const TaskHandle d = jobs.submit(
      [&] {
        const int sum = value_b.load(std::memory_order_acquire) + value_c.load(std::memory_order_acquire);
        value_d.store(sum, std::memory_order_release);
      },
      std::span<const TaskHandle>{bc});

  d.wait();
  jobs.waitAll();

  CHECK(d.ready());
  CHECK(value_d.load(std::memory_order_acquire) == 24);
}

// deps 为空、或包含已完成/默认空句柄时，任务立即可就绪执行。
TEST_CASE("job_deps: empty, default, and already-completed handles do not block execution", "[job][job_deps]") {
  JobSystem jobs(2);

  std::atomic<int> count{0};
  const TaskHandle done = jobs.submit([&] { count.fetch_add(1, std::memory_order_relaxed); });
  done.wait();
  REQUIRE(done.ready());

  const TaskHandle empty_deps =
      jobs.submit([&] { count.fetch_add(10, std::memory_order_relaxed); }, std::span<const TaskHandle>{});

  const TaskHandle mixed_deps =
      jobs.submit([&] { count.fetch_add(100, std::memory_order_relaxed); }, {TaskHandle{}, done});

  empty_deps.wait();
  mixed_deps.wait();
  jobs.waitAll();

  CHECK(empty_deps.ready());
  CHECK(mixed_deps.ready());
  CHECK(count.load(std::memory_order_relaxed) == 111);
}

// deps 中重复出现同一个未完成句柄 {a, a}：a 完成后 b 必须正常执行且只执行一次。
TEST_CASE("job_deps: duplicate handle in deps runs successor exactly once", "[job][job_deps]") {
  JobSystem jobs(2);

  std::atomic<bool> a_done{false};
  std::atomic<int> b_runs{0};

  const TaskHandle a = jobs.submit([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    a_done.store(true, std::memory_order_release);
  });

  const TaskHandle b = jobs.submit(
      [&] {
        if (a_done.load(std::memory_order_acquire)) {
          b_runs.fetch_add(1, std::memory_order_relaxed);
        }
      },
      {a, a});

  b.wait();
  jobs.waitAll();

  CHECK(b.ready());
  CHECK(b_runs.load(std::memory_order_relaxed) == 1);
}

// waitAll 必须覆盖尚未就绪的依赖链（A -> B -> C -> D），不能在中间间隙提前返回。
TEST_CASE("job_deps: waitAll waits for entire dependency chain", "[job][job_deps]") {
  JobSystem jobs(2);

  std::atomic<int> stage{0};

  const TaskHandle a = jobs.submit([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    stage.store(1, std::memory_order_release);
  });

  const TaskHandle b = jobs.submit(
      [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (stage.load(std::memory_order_acquire) == 1) {
          stage.store(2, std::memory_order_release);
        }
      },
      {a});

  const TaskHandle c = jobs.submit(
      [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (stage.load(std::memory_order_acquire) == 2) {
          stage.store(3, std::memory_order_release);
        }
      },
      {b});

  const TaskHandle d = jobs.submit(
      [&] {
        if (stage.load(std::memory_order_acquire) == 3) {
          stage.store(4, std::memory_order_release);
        }
      },
      {c});

  jobs.waitAll();

  CHECK(d.ready());
  CHECK(stage.load(std::memory_order_acquire) == 4);
}

// workerCount == 0 内联模式下，带依赖的 submit 同样按序内联完成。
TEST_CASE("job_deps: zero workers execute dependent tasks inline", "[job][job_deps]") {
  JobSystem jobs(0);

  int value = 1;
  const TaskHandle a = jobs.submit([&] { value += 2; });
  const TaskHandle b = jobs.submit([&] { value *= 5; }, {a});

  CHECK(a.ready());
  CHECK(b.ready());
  CHECK(value == 15);
}

// 随机 DAG 压力用例：64 个节点，每个节点依赖 0~2 个更早的节点。
// 用全局递增票据记录每个任务的开始/结束序号，断言每条边的拓扑顺序
// （后继开始票号 > 前置结束票号），且每个任务恰好执行一次。
TEST_CASE("job_deps: random DAG respects every edge and runs each task exactly once", "[job][job_deps]") {
  constexpr std::size_t kNodes = 64;

  for (unsigned seed = 1; seed <= 8; ++seed) {
    JobSystem jobs(4);

    std::mt19937 rng{seed};
    std::vector<std::vector<std::size_t>> preds(kNodes);

    for (std::size_t node = 1; node < kNodes; ++node) {
      if ((rng() % 100) < 70) {
        preds[node].push_back(rng() % node);
      }
      if ((rng() % 100) < 40) {
        const std::size_t extra = rng() % node;
        if (preds[node].empty() || preds[node].front() != extra) {
          preds[node].push_back(extra);
        }
      }
    }

    std::atomic<int> ticket{0};
    std::vector<int> start_ticket(kNodes, 0);
    std::vector<int> end_ticket(kNodes, 0);
    std::vector<std::atomic<int>> runs(kNodes);
    for (auto& hit : runs) {
      hit.store(0);
    }

    std::vector<TaskHandle> handles(kNodes);
    for (std::size_t node = 0; node < kNodes; ++node) {
      std::vector<TaskHandle> deps;
      for (std::size_t pred : preds[node]) {
        deps.push_back(handles[pred]);
      }

      // 延迟在主线程预先算好并按值捕获：rng 不能被 worker 并发使用。
      const std::chrono::microseconds delay{static_cast<int>(rng() % 300)};
      handles[node] = jobs.submit(
          [&, node, delay] {
            start_ticket[node] = ticket.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(delay);
            end_ticket[node] = ticket.fetch_add(1, std::memory_order_relaxed);
            runs[node].fetch_add(1, std::memory_order_relaxed);
          },
          std::span<const TaskHandle>{deps});
    }

    jobs.waitAll();

    for (std::size_t node = 0; node < kNodes; ++node) {
      CHECK(handles[node].ready());
      CHECK(runs[node].load(std::memory_order_relaxed) == 1);
      for (std::size_t pred : preds[node]) {
        CHECK(end_ticket[pred] < start_ticket[node]);
      }
    }
  }
}
