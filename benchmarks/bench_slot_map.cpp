#include <yr/core/slot_map.h>
#include <yr/core/timer.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

  constexpr std::uint32_t kSeed = 0x5EED1234u;
  constexpr std::size_t kWarmupCount = 3;
  constexpr std::size_t kSampleCount = 7;
  constexpr std::size_t kIterationPasses = 8;

  struct Payload {
    std::uint64_t id;
    std::array<std::uint64_t, 7> data;
  };

  struct TimedSample {
    yr::core::Duration elapsed;
    std::uint64_t checksum;
  };

  struct BenchmarkResult {
    double medianSeconds;
    double minSeconds;
    double maxSeconds;
    std::uint64_t checksum;
  };

  class SlotMapStore {
  public:
    using Key = yr::core::Handle<Payload>;

    void reserve(std::size_t count) { values_.reserve(count); }
    Key insert(Payload value) { return values_.insert(std::move(value)); }
    bool erase(Key key) { return values_.erase(key); }
    [[nodiscard]] const Payload* get(Key key) const noexcept { return values_.get(key); }

    template <typename F> void forEach(F&& function) const {
      for (const Payload& value : values_) {
        function(value);
      }
    }

  private:
    yr::core::SlotMap<Payload> values_;
  };

  class UnorderedMapStore {
  public:
    using Key = std::uint64_t;

    void reserve(std::size_t count) { values_.reserve(count); }

    Key insert(Payload value) {
      const Key key = nextKey_++;
      values_.emplace(key, std::move(value));
      return key;
    }

    bool erase(Key key) { return values_.erase(key) == 1; }

    [[nodiscard]] const Payload* get(Key key) const noexcept {
      const auto it = values_.find(key);
      return it == values_.end() ? nullptr : &it->second;
    }

    template <typename F> void forEach(F&& function) const {
      for (const auto& [key, value] : values_) {
        (void)key;
        function(value);
      }
    }

  private:
    Key nextKey_ = 0;
    std::unordered_map<Key, Payload> values_;
  };

  class VectorFreeListStore {
  public:
    using Key = std::uint32_t;

    void reserve(std::size_t count) {
      values_.reserve(count);
      freeList_.reserve(count);
    }

    Key insert(Payload value) {
      if (freeList_.empty()) {
        const Key key = static_cast<Key>(values_.size());
        values_.emplace_back(std::move(value));
        return key;
      }

      const Key key = freeList_.back();
      freeList_.pop_back();
      values_[key].emplace(std::move(value));
      return key;
    }

    bool erase(Key key) {
      if (key >= values_.size() || !values_[key].has_value()) {
        return false;
      }
      values_[key].reset();
      freeList_.push_back(key);
      return true;
    }

    [[nodiscard]] const Payload* get(Key key) const noexcept {
      if (key >= values_.size() || !values_[key].has_value()) {
        return nullptr;
      }
      return &*values_[key];
    }

    template <typename F> void forEach(F&& function) const {
      for (const std::optional<Payload>& value : values_) {
        if (value.has_value()) {
          function(*value);
        }
      }
    }

  private:
    std::vector<std::optional<Payload>> values_;
    std::vector<Key> freeList_;
  };

  [[nodiscard]] std::vector<Payload> makePayloads(std::size_t count) {
    std::vector<Payload> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const std::uint64_t id = static_cast<std::uint64_t>(i + 1);
      values.push_back({id, {id * 3, id * 5, id * 7, id * 11, id * 13, id * 17, id * 19}});
    }
    return values;
  }

  [[nodiscard]] std::vector<std::size_t> makeLookupOrder(std::size_t count) {
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::mt19937 rng(kSeed);
    std::shuffle(order.begin(), order.end(), rng);
    return order;
  }

  [[nodiscard]] double toSeconds(yr::core::Duration duration) {
    return std::chrono::duration<double>(duration).count();
  }

  template <typename F> BenchmarkResult measure(F&& function) {
    for (std::size_t i = 0; i < kWarmupCount; ++i) {
      (void)function();
    }

    std::vector<yr::core::Duration> samples;
    samples.reserve(kSampleCount);
    std::uint64_t checksum = 0;
    for (std::size_t i = 0; i < kSampleCount; ++i) {
      const TimedSample sample = function();
      samples.push_back(sample.elapsed);
      checksum ^= sample.checksum;
    }

    std::sort(samples.begin(), samples.end());
    return {
        toSeconds(samples[samples.size() / 2]),
        toSeconds(samples.front()),
        toSeconds(samples.back()),
        checksum,
    };
  }

  template <typename Store> TimedSample benchmarkInsert(const std::vector<Payload>& values) {
    Store store;
    store.reserve(values.size());

    yr::core::ScopeTimer timer;
    for (const Payload& value : values) {
      (void)store.insert(value);
    }
    const yr::core::Duration elapsed = timer.elapsed();

    std::uint64_t checksum = 0;
    store.forEach([&checksum](const Payload& value) { checksum += value.id; });
    return {elapsed, checksum};
  }

  template <typename Store>
  TimedSample benchmarkLookup(const std::vector<Payload>& values, const std::vector<std::size_t>& order) {
    Store store;
    store.reserve(values.size());
    std::vector<typename Store::Key> keys;
    keys.reserve(values.size());
    for (const Payload& value : values) {
      keys.push_back(store.insert(value));
    }

    std::uint64_t checksum = 0;
    yr::core::ScopeTimer timer;
    for (const std::size_t index : order) {
      const Payload* value = store.get(keys[index]);
      if (value != nullptr) {
        checksum += value->id + value->data[0];
      }
    }
    return {timer.elapsed(), checksum};
  }

  template <typename Store> TimedSample benchmarkIteration(const std::vector<Payload>& values) {
    Store store;
    store.reserve(values.size());
    for (const Payload& value : values) {
      (void)store.insert(value);
    }

    std::uint64_t checksum = 0;
    yr::core::ScopeTimer timer;
    for (std::size_t pass = 0; pass < kIterationPasses; ++pass) {
      store.forEach([&checksum](const Payload& value) { checksum += value.id + value.data[0]; });
    }
    return {timer.elapsed(), checksum};
  }

  template <typename Store>
  TimedSample benchmarkErase(const std::vector<Payload>& values, const std::vector<std::size_t>& eraseOrder) {
    Store store;
    store.reserve(values.size());
    std::vector<typename Store::Key> keys;
    keys.reserve(values.size());
    for (const Payload& value : values) {
      keys.push_back(store.insert(value));
    }

    std::uint64_t erasedCount = 0;
    yr::core::ScopeTimer timer;
    for (const std::size_t index : eraseOrder) {
      erasedCount += store.erase(keys[index]) ? 1u : 0u;
    }
    const yr::core::Duration elapsed = timer.elapsed();

    std::uint64_t remainingChecksum = 0;
    store.forEach([&remainingChecksum](const Payload& value) { remainingChecksum += value.id; });
    return {elapsed, remainingChecksum ^ erasedCount};
  }

  void printResult(std::string_view operation, std::string_view container, const BenchmarkResult& result) {
    std::cout << std::left << std::setw(12) << operation << std::setw(22) << container << std::right << std::setw(16)
              << std::scientific << std::setprecision(6) << result.medianSeconds << std::setw(16) << result.minSeconds
              << std::setw(16) << result.maxSeconds << std::setw(20) << result.checksum << '\n';
  }

  template <typename Store>
  void runContainer(std::string_view name, const std::vector<Payload>& values,
                    const std::vector<std::size_t>& lookupOrder, const std::vector<std::size_t>& eraseOrder) {
    printResult("insert", name, measure([&values]() { return benchmarkInsert<Store>(values); }));
    printResult("lookup", name,
                measure([&values, &lookupOrder]() { return benchmarkLookup<Store>(values, lookupOrder); }));
    printResult("erase 50%", name,
                measure([&values, &eraseOrder]() { return benchmarkErase<Store>(values, eraseOrder); }));
    printResult("iteration", name, measure([&values]() { return benchmarkIteration<Store>(values); }));
  }

} // namespace

int main() {
  constexpr std::array<std::size_t, 3> kElementCounts = {10'000, 100'000, 1'000'000};

  std::cout << "SlotMap benchmark (Payload=" << sizeof(Payload) << " bytes, warmup=" << kWarmupCount
            << ", samples=" << kSampleCount << ", iteration passes=" << kIterationPasses << ")\n";

  for (const std::size_t count : kElementCounts) {
    const std::vector<Payload> values = makePayloads(count);
    const std::vector<std::size_t> lookupOrder = makeLookupOrder(count);
    const std::vector<std::size_t> eraseOrder(lookupOrder.begin(), lookupOrder.begin() + count / 2);

    std::cout << "\nElements: " << count << '\n';
    std::cout << std::left << std::setw(12) << "operation" << std::setw(22) << "container" << std::right
              << std::setw(16) << "median(s)" << std::setw(16) << "min(s)" << std::setw(16) << "max(s)" << std::setw(20)
              << "checksum" << '\n';

    runContainer<SlotMapStore>("SlotMap", values, lookupOrder, eraseOrder);
    runContainer<UnorderedMapStore>("unordered_map", values, lookupOrder, eraseOrder);
    runContainer<VectorFreeListStore>("vector+freelist", values, lookupOrder, eraseOrder);
  }
}
