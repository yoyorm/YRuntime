#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <yr/job/mt_queue.h>

namespace {

  using yr::job::MtQueue;

  void waitUntil(const std::atomic<bool>& flag) {
    while (!flag.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }

  void waitUntilAtLeast(const std::atomic<int>& counter, int expected) {
    while (counter.load(std::memory_order_acquire) < expected) {
      std::this_thread::yield();
    }
  }

  // 有界自旋等待：丢了唤醒的实现会在这里超时，而不是卡住整个测试进程。
  template <typename Predicate> bool spinUntil(Predicate predicate, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }
      std::this_thread::yield();
    }
    return true;
  }

} // namespace

TEST_CASE("mt_queue: single-thread FIFO", "[job][mt_queue]") {
  MtQueue<int> queue;
  CHECK(queue.size() == 0);
  CHECK_FALSE(queue.closed());

  queue.push(1);
  queue.push(2);
  queue.push(3);
  CHECK(queue.size() == 3);

  int value = 0;
  REQUIRE(queue.try_pop(value));
  CHECK(value == 1);
  REQUIRE(queue.try_pop(value));
  CHECK(value == 2);
  REQUIRE(queue.try_pop(value));
  CHECK(value == 3);
  CHECK_FALSE(queue.try_pop(value));
  CHECK(queue.size() == 0);

  queue.close();
  CHECK(queue.closed());
}

TEST_CASE("mt_queue: supports move-only values", "[job][mt_queue]") {
  MtQueue<std::unique_ptr<int>> queue;
  queue.push(std::make_unique<int>(7));

  std::unique_ptr<int> out;
  REQUIRE(queue.try_pop(out));
  REQUIRE(out != nullptr);
  CHECK(*out == 7);
  CHECK_FALSE(queue.try_pop(out));

  queue.close();
}

TEST_CASE("mt_queue: wait_pop blocks until an item is pushed", "[job][mt_queue]") {
  MtQueue<int> queue;
  std::atomic<bool> entered{false};
  std::atomic<bool> returned{false};
  int value = 0;
  bool popped = false;

  std::thread consumer([&] {
    entered.store(true, std::memory_order_release);
    popped = queue.wait_pop(value);
    returned.store(true, std::memory_order_release);
  });

  waitUntil(entered);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK_FALSE(returned.load(std::memory_order_acquire));

  queue.push(42);
  consumer.join();

  CHECK(popped);
  CHECK(value == 42);
  CHECK(returned.load(std::memory_order_acquire));
  queue.close();
}

TEST_CASE("mt_queue: close wakes a waiting consumer", "[job][mt_queue]") {
  MtQueue<int> queue;
  std::atomic<bool> entered{false};
  std::atomic<bool> returned{false};
  int value = 0;
  bool popped = true;

  std::thread consumer([&] {
    entered.store(true, std::memory_order_release);
    popped = queue.wait_pop(value);
    returned.store(true, std::memory_order_release);
  });

  waitUntil(entered);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK_FALSE(returned.load(std::memory_order_acquire));

  queue.close();
  consumer.join();

  CHECK_FALSE(popped);
  CHECK(returned.load(std::memory_order_acquire));
  CHECK(queue.closed());
}

// 抓 notify_one / notify_all 混淆：close() 必须唤醒所有等待者。
//
// 注意：如果实现只唤醒一个等待者，本用例会在 join 处挂住而不是断言失败——
// 丢唤醒无法用断言表达。因此 ctest 侧给 YrTests_job 设了 TIMEOUT，
// 挂住会被转成超时失败。前面的 spinUntil 只负责先打印一条可读的断言。
TEST_CASE("mt_queue: close wakes every waiting consumer", "[job][mt_queue]") {
  constexpr int kWaiters = 4;

  MtQueue<int> queue;
  std::atomic<int> waiting{0};
  std::atomic<int> returned{0};
  std::atomic<int> drained{0};

  std::vector<std::thread> consumers;
  consumers.reserve(kWaiters);
  for (int index = 0; index < kWaiters; ++index) {
    consumers.emplace_back([&] {
      waiting.fetch_add(1, std::memory_order_release);
      int value = 0;
      if (!queue.wait_pop(value)) {
        drained.fetch_add(1, std::memory_order_release);
      }
      returned.fetch_add(1, std::memory_order_release);
    });
  }

  waitUntilAtLeast(waiting, kWaiters);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(returned.load(std::memory_order_acquire) == 0);

  queue.close();

  const bool allReturned =
      spinUntil([&] { return returned.load(std::memory_order_acquire) == kWaiters; }, std::chrono::milliseconds(2000));
  CHECK(allReturned);
  CHECK(drained.load(std::memory_order_acquire) == kWaiters);

  for (std::thread& thread : consumers) {
    thread.join();
  }
}

TEST_CASE("mt_queue: close drains already queued items", "[job][mt_queue]") {
  MtQueue<int> queue;
  queue.push(1);
  queue.push(2);
  queue.close();

  int value = 0;
  REQUIRE(queue.try_pop(value));
  CHECK(value == 1);
  REQUIRE(queue.try_pop(value));
  CHECK(value == 2);
  CHECK_FALSE(queue.try_pop(value));
  CHECK_FALSE(queue.wait_pop(value));
  CHECK(queue.size() == 0);
}

TEST_CASE("mt_queue: multiple producers and consumers", "[job][mt_queue]") {
  constexpr int kProducers = 4;
  constexpr int kPerProducer = 500;
  constexpr int kTotal = kProducers * kPerProducer;

  // 元素值是 producer * kPerProducer + item，期望值必须包含 producer 偏移。
  // 按推送顺序直接累加，比手写闭式公式更难写错。
  long long expectedSum = 0;
  for (int producer = 0; producer < kProducers; ++producer) {
    for (int item = 0; item < kPerProducer; ++item) {
      expectedSum += producer * kPerProducer + item;
    }
  }

  MtQueue<int> queue;
  std::atomic<int> count{0};
  std::atomic<long long> sum{0};

  std::vector<std::thread> consumers;
  consumers.reserve(kProducers);
  for (int index = 0; index < kProducers; ++index) {
    consumers.emplace_back([&] {
      int value = 0;
      while (queue.wait_pop(value)) {
        count.fetch_add(1, std::memory_order_relaxed);
        sum.fetch_add(value, std::memory_order_relaxed);
      }
    });
  }

  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  for (int producer = 0; producer < kProducers; ++producer) {
    producers.emplace_back([&, producer] {
      for (int item = 0; item < kPerProducer; ++item) {
        queue.push(producer * kPerProducer + item);
      }
    });
  }

  for (std::thread& thread : producers) {
    thread.join();
  }
  queue.close();
  for (std::thread& thread : consumers) {
    thread.join();
  }

  CHECK(count.load(std::memory_order_relaxed) == kTotal);
  CHECK(sum.load(std::memory_order_relaxed) == expectedSum);
}

TEST_CASE("mt_queue: push after close is rejected", "[job][mt_queue]") {
  MtQueue<int> queue;
  queue.close();
  CHECK(queue.closed());
  CHECK_THROWS_AS(queue.push(1), std::logic_error);
}

// move-only 元素的拒绝路径：确认 push 不会为了“先检查再入队”而要求 T 可拷贝，
// 也确认被拒的元素没有留在队列里。
TEST_CASE("mt_queue: push after close is rejected for move-only values", "[job][mt_queue]") {
  MtQueue<std::unique_ptr<int>> queue;
  queue.close();
  CHECK(queue.closed());

  CHECK_THROWS_AS(queue.push(std::make_unique<int>(5)), std::logic_error);

  std::unique_ptr<int> out;
  CHECK_FALSE(queue.try_pop(out));
  CHECK(out == nullptr);
  CHECK(queue.size() == 0);
}

TEST_CASE("mt_queue: size and closed reflect queue state", "[job][mt_queue]") {
  MtQueue<int> queue;
  CHECK(queue.size() == 0);
  CHECK_FALSE(queue.closed());

  queue.push(11);
  CHECK(queue.size() == 1);

  int value = 0;
  REQUIRE(queue.try_pop(value));
  CHECK(value == 11);
  CHECK(queue.size() == 0);

  queue.close();
  CHECK(queue.closed());
  queue.close();
  CHECK(queue.closed());
}
