#include <catch2/catch_test_macros.hpp>

#include <yr/core/handle.h>

#include <cstdint>
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace {

  using IntHandle = yr::core::Handle<int>;
  using FloatHandle = yr::core::Handle<float>;

  // 从 index / generation 拼出裸值。
  // 注意：Handle<T>::make() 是私有的（只授权给 SlotMap），测试无法直接调用，
  // 所以这里按文档的位布局手工构造 —— 这同时验证了"布局本身是公开契约"。
  constexpr uint64_t rawOf(uint32_t index, uint64_t generation) noexcept {
    return (generation << yr::core::HandleBits::kIndexBits) | index;
  }

} // namespace

// ============================================================================
// 编译期断言：零成本、先于任何运行期测试失败
// ============================================================================

static_assert(std::is_trivially_copyable_v<IntHandle>);
static_assert(std::is_default_constructible_v<IntHandle>);
static_assert(sizeof(IntHandle) == 8);

// 类型隔离：Handle<A> 与 Handle<B> 必须互不兼容
static_assert(!std::is_convertible_v<IntHandle, FloatHandle>);
static_assert(!std::is_constructible_v<FloatHandle, IntHandle>);
static_assert(!std::is_assignable_v<FloatHandle&, IntHandle>);

// 不允许从裸整数隐式构造（否则类型安全和"唯一来源"都失效）
static_assert(!std::is_constructible_v<IntHandle, uint64_t>);
static_assert(!std::is_convertible_v<uint64_t, IntHandle>);

// 同一模板的不同实例，布局必须完全相同（T 是幽灵类型，不占空间）
static_assert(sizeof(IntHandle) == sizeof(FloatHandle));

// 位布局自洽
static_assert(yr::core::HandleBits::kIndexBits + yr::core::HandleBits::kGenerationBits == 64);

// ============================================================================
// 无效值语义
// ============================================================================

TEST_CASE("handle: 默认构造是 invalid", "[core][handle]") {
  const IntHandle h{};

  CHECK_FALSE(h.valid());
  CHECK(h.raw() == 0);
  CHECK(h.index() == 0);
  CHECK(h.generation() == 0);
  CHECK(h == IntHandle::invalid());
}

TEST_CASE("handle: invalid() 与默认构造等价", "[core][handle]") {
  CHECK(IntHandle::invalid() == IntHandle{});
  CHECK(IntHandle::invalid().raw() == 0);

  // invalid 与自身相等、哈希一致 —— 否则放进哈希容器会出问题
  CHECK(IntHandle::invalid() == IntHandle::invalid());
  CHECK(std::hash<IntHandle>{}(IntHandle::invalid()) == std::hash<IntHandle>{}(IntHandle{}));
}

TEST_CASE("handle: 所有无效 handle 互相相等", "[core][handle]") {
  // raw == 0 是唯一的无效表示，所以它们必然相等
  CHECK(IntHandle::fromRaw(0) == IntHandle{});
  CHECK_FALSE(IntHandle::fromRaw(0).valid());
}

// ============================================================================
// 位打包：index / generation 的往返
// ============================================================================

TEST_CASE("handle: index 与 generation 正确往返", "[core][handle]") {
  const IntHandle h = IntHandle::fromRaw(rawOf(7, 5));

  CHECK(h.index() == 7);
  CHECK(h.generation() == 5);
  CHECK(h.raw() == rawOf(7, 5));
  CHECK(h.valid());
}

TEST_CASE("handle: index 0 是合法槽位，不是 invalid", "[core][handle]") {
  // invalid 靠 raw == 0 表示，而不是 index == 0。
  // generation 从 1 起，所以 (index=0, generation=1) 是合法的。
  const IntHandle h = IntHandle::fromRaw(rawOf(0, 1));

  CHECK(h.index() == 0);
  CHECK(h.generation() == 1);
  CHECK(h.valid()); // ← 关键：index 0 合法
  CHECK(h != IntHandle{});
}

TEST_CASE("handle: index 字段被正确掩码，不污染 generation", "[core][handle]") {
  // index 占满 24 位时，不能溢出到 generation 里
  constexpr uint32_t kMaxIndex = static_cast<uint32_t>(yr::core::HandleBits::kIndexMask);
  const IntHandle h = IntHandle::fromRaw(rawOf(kMaxIndex, 1));

  CHECK(h.index() == kMaxIndex);
  CHECK(h.generation() == 1);
}

TEST_CASE("handle: generation 取较大值时不被截断", "[core][handle]") {
  // generation 有 40 位，用一个大值验证右移读取没丢位
  constexpr uint64_t kBigGen = (uint64_t{1} << 40) - 1;
  const IntHandle h = IntHandle::fromRaw(rawOf(3, kBigGen));

  CHECK(h.index() == 3);
  CHECK(h.generation() == kBigGen);
}

// ============================================================================
// 相等语义 + ABA 防护
// ============================================================================

TEST_CASE("handle: 相等比较只依赖 raw", "[core][handle]") {
  const IntHandle a = IntHandle::fromRaw(rawOf(3, 1));
  const IntHandle b = IntHandle::fromRaw(rawOf(3, 1));
  const IntHandle c = IntHandle::fromRaw(rawOf(4, 1));

  CHECK(a == b);
  CHECK_FALSE(a != b);
  CHECK(a != c);
}

TEST_CASE("handle: 同一 index 不同 generation 必须不等（ABA 核心）", "[core][handle]") {
  // 这就是 slot 复用后的场景：
  //   旧 handle = {index 3, generation 1}
  //   对象销毁后 slot 3 空闲，generation 自增到 2
  //   新对象 B 复用 slot 3 -> 新 handle = {index 3, generation 2}
  const IntHandle old_handle = IntHandle::fromRaw(rawOf(3, 1));
  const IntHandle new_handle = IntHandle::fromRaw(rawOf(3, 2));

  CHECK(old_handle.index() == new_handle.index());           // index 相同
  CHECK(old_handle.generation() != new_handle.generation()); // generation 不同
  CHECK(old_handle != new_handle);                           // 所以句柄不同 ← ABA 防护
  CHECK(old_handle.raw() != new_handle.raw());
}

// ============================================================================
// 哈希
// ============================================================================

TEST_CASE("handle: 相等则哈希必相等（哈希契约）", "[core][handle]") {
  const IntHandle a = IntHandle::fromRaw(rawOf(123, 45));
  const IntHandle b = IntHandle::fromRaw(rawOf(123, 45));

  REQUIRE(a == b);
  CHECK(std::hash<IntHandle>{}(a) == std::hash<IntHandle>{}(b));
}

TEST_CASE("handle: 可作为 unordered_set 的 key", "[core][handle]") {
  std::unordered_set<IntHandle> set;

  const IntHandle a = IntHandle::fromRaw(rawOf(1, 1));
  const IntHandle b = IntHandle::fromRaw(rawOf(2, 1));
  const IntHandle c = IntHandle::fromRaw(rawOf(1, 2)); // 与 a 同 index、不同 generation

  set.insert(a);
  set.insert(b);
  set.insert(c);
  set.insert(a); // 重复插入应被忽略

  CHECK(set.size() == 3); // == 与 hash 一致才能得到 3
  CHECK(set.contains(a));
  CHECK(set.contains(b));
  CHECK(set.contains(c));
  CHECK_FALSE(set.contains(IntHandle::fromRaw(rawOf(9, 9))));
}

TEST_CASE("handle: 可作为 unordered_map 的 key", "[core][handle]") {
  std::unordered_map<IntHandle, int> map;

  const IntHandle a = IntHandle::fromRaw(rawOf(10, 1));
  const IntHandle b = IntHandle::fromRaw(rawOf(10, 2));

  map[a] = 100;
  map[b] = 200;
  map[a] = 111; // 覆写，不应新增条目

  CHECK(map.size() == 2);
  CHECK(map.at(a) == 111);
  CHECK(map.at(b) == 200);
}
