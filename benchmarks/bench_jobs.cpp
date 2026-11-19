// E11 · JobSystem：任务提交 / 依赖图 / parallel_for 的串行 vs 并行
//
// 这是手动运行的性能实验（不注册到 ctest）。目的不是"刷分"，而是回答：
//   1. 固定任务粒度下，worker 数增加时总耗时如何变化（加速比）；
//   2. 任务太小时提交/唤醒开销是否吃掉收益（batch size 扫描）；
//   3. parallel_for 的批次大小如何影响总耗时。
//
// 用法：
//   cmake --preset benchmark && cmake --build --preset benchmark
//   ./build/benchmark/benchmarks/YrBench_jobs

#include <yr/job/job_system.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace {

  using Clock = std::chrono::steady_clock;

  constexpr std::size_t kSamples = 5;
  constexpr std::uint32_t kWorkIterations = 4096;

  // 每个任务做的纯计算量：xorshift 循环，结果写回 index，防止被优化掉。
  std::uint64_t burn(std::uint64_t seed) {
    std::uint64_t state = seed | 1u;
    for (std::uint32_t i = 0; i < kWorkIterations; ++i) {
      state ^= state << 13;
      state ^= state >> 7;
      state ^= state << 17;
    }
    return state;
  }

  double median_of(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
  }

  double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
  }

  std::uint64_t checksum(const std::vector<std::uint64_t>& values) {
    std::uint64_t sum = 0;
    for (std::uint64_t value : values) {
      sum ^= value;
    }
    return sum;
  }

  // 串行基线：同样的工作量，没有任何线程/队列开销。
  double time_serial(std::size_t tasks, std::vector<std::uint64_t>& results) {
    const auto start = Clock::now();
    for (std::size_t index = 0; index < tasks; ++index) {
      results[index] = burn(index + 1);
    }
    return seconds_since(start);
  }

  // 每个任务单独 submit + waitAll。
  double time_submit(std::size_t tasks, std::size_t threads, std::vector<std::uint64_t>& results) {
    yr::job::JobSystem jobs(threads);
    const auto start = Clock::now();
    for (std::size_t index = 0; index < tasks; ++index) {
      (void)jobs.submit([&results, index] { results[index] = burn(index + 1); });
    }
    jobs.waitAll();
    return seconds_since(start);
  }

  // parallel_for：批次大小固定为 1（每个元素一个任务）。
  double time_parallel_for(std::size_t tasks, std::size_t threads, std::size_t batchSize,
                           std::vector<std::uint64_t>& results) {
    yr::job::JobSystem jobs(threads);
    const auto start = Clock::now();
    jobs.parallelFor(tasks, batchSize, [&results](std::size_t begin, std::size_t end) {
      for (std::size_t index = begin; index < end; ++index) {
        results[index] = burn(index + 1);
      }
    });
    return seconds_since(start);
  }

  template <typename Fn> double sample(Fn&& run) {
    double best = 0.0;
    for (std::size_t i = 0; i < kSamples; ++i) {
      const double seconds = run();
      if (i == 0 || seconds < best) {
        best = seconds;
      }
    }
    return best;
  }

  void print_row(std::size_t tasks, std::size_t threads, double serial, double submit, double parallelFor) {
    const double submitSpeedup = submit > 0.0 ? serial / submit : 0.0;
    const double parallelSpeedup = parallelFor > 0.0 ? serial / parallelFor : 0.0;
    std::cout << std::left << std::setw(9) << tasks << std::setw(10) << threads << std::right << std::fixed
              << std::setprecision(1) << std::setw(11) << serial * 1e3 << std::setw(12) << submit * 1e3 << std::setw(10)
              << submitSpeedup << std::setw(14) << parallelFor * 1e3 << std::setw(10) << parallelSpeedup << '\n';
  }

} // namespace

int main() {
  const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
  std::cout << "YrBench_jobs · hardware_concurrency() = " << hardware << ", work per task = " << kWorkIterations
            << " xorshift iterations\n\n";

  const std::vector<std::size_t> taskCounts{1000, 10000};
  std::vector<std::size_t> threadCounts{1, 2, 4, 8};
  threadCounts.erase(std::remove_if(threadCounts.begin(), threadCounts.end(),
                                    [hardware](std::size_t count) { return count > hardware; }),
                     threadCounts.end());

  {
    std::vector<std::uint64_t> results(10000, 0);
    const double serial = sample([&] { return time_serial(10000, results); });
    std::cout << "单任务耗时（串行）：" << std::fixed << std::setprecision(2) << serial * 1e6 / 10000.0 << " us\n\n";
  }

  std::cout << std::left << std::setw(9) << "tasks" << std::setw(10) << "threads" << std::right << std::setw(11)
            << "serial ms" << std::setw(12) << "submit ms" << std::setw(10) << "speedup" << std::setw(14)
            << "pfor(1) ms" << std::setw(10) << "speedup" << '\n';
  std::cout << std::string(76, '-') << '\n';

  for (const std::size_t tasks : taskCounts) {
    std::vector<std::uint64_t> results(tasks, 0);

    const double serial = sample([&] { return time_serial(tasks, results); });
    const std::uint64_t serialChecksum = checksum(results);

    for (const std::size_t threads : threadCounts) {
      const double submit = sample([&] { return time_submit(tasks, threads, results); });
      const std::uint64_t submitChecksum = checksum(results);
      const double parallelFor = sample([&] { return time_parallel_for(tasks, threads, 1, results); });
      const std::uint64_t parallelChecksum = checksum(results);

      if (submitChecksum != serialChecksum || parallelChecksum != serialChecksum) {
        std::cout << "  [warn] checksum mismatch at tasks=" << tasks << " threads=" << threads << '\n';
      }
      print_row(tasks, threads, serial, submit, parallelFor);
    }
  }

  // batch size 扫描：验证"任务太小 → 提交/唤醒开销吃掉收益"。
  {
    constexpr std::size_t kTasks = 10000;
    std::vector<std::uint64_t> results(kTasks, 0);
    std::cout << "\nbatch size 扫描（tasks = " << kTasks << ", threads = " << hardware << "）\n";
    std::cout << std::left << std::setw(12) << "batch(1 elem)" << std::right << std::setw(14) << "pfor ms" << '\n';
    std::cout << std::string(26, '-') << '\n';
    for (const std::size_t batch : {1u, 8u, 64u, 512u, 4096u}) {
      const double parallelFor = sample([&] { return time_parallel_for(kTasks, hardware, batch, results); });
      std::cout << std::left << std::setw(12) << batch << std::right << std::fixed << std::setprecision(1)
                << std::setw(14) << parallelFor * 1e3 << '\n';
    }
  }

  return 0;
}