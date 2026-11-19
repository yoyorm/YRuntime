#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <numeric>
#include <thread>
#include <vector>

#include <yr/job/job_system.h>

namespace {

  using yr::job::JobSystem;

} // namespace

// 每个下标必须恰好被访问一次，且总和正确。
TEST_CASE("parallel_for: covers every index exactly once", "[job][parallel_for]") {
  JobSystem jobs(4);

  constexpr std::size_t kCount = 1000;
  std::vector<std::atomic<int>> hits(kCount);
  for (auto& hit : hits) {
    hit.store(0, std::memory_order_relaxed);
  }

  std::atomic<long long> sum{0};

  jobs.parallelFor(kCount, 64, [&](std::size_t begin, std::size_t end) {
    long long local = 0;
    for (std::size_t index = begin; index < end; ++index) {
      hits[index].fetch_add(1, std::memory_order_relaxed);
      local += static_cast<long long>(index);
    }
    sum.fetch_add(local, std::memory_order_relaxed);
  });

  for (std::size_t index = 0; index < kCount; ++index) {
    CHECK(hits[index].load(std::memory_order_relaxed) == 1);
  }
  CHECK(sum.load(std::memory_order_relaxed) == static_cast<long long>(kCount) * static_cast<long long>(kCount - 1) / 2);
}

// batchSize > count 时只应产生一个批次 (0, count)。
TEST_CASE("parallel_for: batch larger than count runs a single batch", "[job][parallel_for]") {
  JobSystem jobs(4);

  std::atomic<int> calls{0};
  std::atomic<std::size_t> seen_begin{1};
  std::atomic<std::size_t> seen_end{0};

  jobs.parallelFor(10, 64, [&](std::size_t begin, std::size_t end) {
    calls.fetch_add(1, std::memory_order_relaxed);
    seen_begin.store(begin, std::memory_order_relaxed);
    seen_end.store(end, std::memory_order_relaxed);
  });

  CHECK(calls.load(std::memory_order_relaxed) == 1);
  CHECK(seen_begin.load(std::memory_order_relaxed) == 0);
  CHECK(seen_end.load(std::memory_order_relaxed) == 10);
}

// count == 0：不提交任何批次，直接返回。
TEST_CASE("parallel_for: count zero does nothing", "[job][parallel_for]") {
  JobSystem jobs(2);

  std::atomic<int> calls{0};
  jobs.parallelFor(0, 16, [&](std::size_t, std::size_t) { calls.fetch_add(1, std::memory_order_relaxed); });

  CHECK(calls.load(std::memory_order_relaxed) == 0);
}

// batchSize == 0 视作 1：每个元素一个批次，仍然每个下标恰好一次。
TEST_CASE("parallel_for: zero batch size means one element per batch", "[job][parallel_for]") {
  JobSystem jobs(3);

  constexpr std::size_t kCount = 64;
  std::vector<std::atomic<int>> hits(kCount);
  for (auto& hit : hits) {
    hit.store(0, std::memory_order_relaxed);
  }

  jobs.parallelFor(kCount, 0, [&](std::size_t begin, std::size_t end) {
    for (std::size_t index = begin; index < end; ++index) {
      hits[index].fetch_add(1, std::memory_order_relaxed);
    }
  });

  for (std::size_t index = 0; index < kCount; ++index) {
    CHECK(hits[index].load(std::memory_order_relaxed) == 1);
  }
}

// 单线程（0 worker）：顺序内联执行，访问顺序必须是 0..count-1。
TEST_CASE("parallel_for: zero workers execute inline in order", "[job][parallel_for]") {
  JobSystem jobs(0);

  constexpr std::size_t kCount = 32;
  std::vector<std::size_t> order;
  order.reserve(kCount);

  jobs.parallelFor(kCount, 8, [&](std::size_t begin, std::size_t end) {
    for (std::size_t index = begin; index < end; ++index) {
      order.push_back(index);
    }
  });

  REQUIRE(order.size() == kCount);
  for (std::size_t index = 0; index < kCount; ++index) {
    CHECK(order[index] == index);
  }
}

// 只等待自己的批次：无关慢任务仍在跑时，parallelFor 也应及时返回。
TEST_CASE("parallel_for: returns without waiting for unrelated slow task", "[job][parallel_for]") {
  JobSystem jobs(2);

  std::atomic<bool> slow_started{false};
  std::atomic<bool> slow_done{false};

  const auto slow = jobs.submit([&] {
    slow_started.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    slow_done.store(true, std::memory_order_release);
  });

  while (!slow_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  std::atomic<int> processed{0};
  jobs.parallelFor(64, 8, [&](std::size_t begin, std::size_t end) {
    processed.fetch_add(static_cast<int>(end - begin), std::memory_order_relaxed);
  });

  CHECK(processed.load(std::memory_order_relaxed) == 64);
  CHECK_FALSE(slow_done.load(std::memory_order_acquire));

  slow.wait();
  CHECK(slow_done.load(std::memory_order_acquire));
}
// 确定性：批划分只是执行策略，不改变语义——两次运行结果逐元素相同，且等于串行。
TEST_CASE("parallel_for: two runs produce identical results", "[job][parallel_for]") {
  JobSystem jobs(4);

  constexpr std::size_t kCount = 4096;
  auto compute = [](std::size_t index) { return (index * 2654435761u) ^ (index >> 3); };

  std::vector<std::uint32_t> serial(kCount, 0);
  for (std::size_t index = 0; index < kCount; ++index) {
    serial[index] = compute(index);
  }

  std::vector<std::uint32_t> first(kCount, 0);
  std::vector<std::uint32_t> second(kCount, 0);

  jobs.parallelFor(kCount, 7, [&](std::size_t begin, std::size_t end) {
    for (std::size_t index = begin; index < end; ++index) {
      first[index] = compute(index);
    }
  });
  jobs.parallelFor(kCount, 7, [&](std::size_t begin, std::size_t end) {
    for (std::size_t index = begin; index < end; ++index) {
      second[index] = compute(index);
    }
  });

  CHECK(first == serial);
  CHECK(second == first);
}

// 不同 batch size 必须得到完全相同的数组。
TEST_CASE("parallel_for: batch size does not change results", "[job][parallel_for]") {
  JobSystem jobs(4);

  constexpr std::size_t kCount = 1024;
  auto compute = [](std::size_t index) { return index * index + 17; };

  std::vector<std::uint64_t> reference(kCount, 0);
  for (std::size_t index = 0; index < kCount; ++index) {
    reference[index] = compute(index);
  }

  for (const std::size_t batch : {std::size_t{1}, std::size_t{7}, std::size_t{64}, std::size_t{4096}}) {
    std::vector<std::uint64_t> results(kCount, 0);
    jobs.parallelFor(kCount, batch, [&](std::size_t begin, std::size_t end) {
      for (std::size_t index = begin; index < end; ++index) {
        results[index] = compute(index);
      }
    });
    CHECK(results == reference);
  }
}
