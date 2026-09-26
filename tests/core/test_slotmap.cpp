#include <catch2/catch_test_macros.hpp>

#include <yr/core/slot_map.h>

#include <string>
#include <vector>

namespace {

  using IntMap = yr::core::SlotMap<int>;
  using IntHandle = yr::core::Handle<int>;

} // namespace

// ============================================================================
// reserve：只预留容量，不创建元素
// ============================================================================

TEST_CASE("slotmap: reserve 不改变 size，也不创建元素", "[core][slotmap]") {
  IntMap sm;
  sm.reserve(64);

  CHECK(sm.capacity() >= 64); // 容量预留了
  CHECK(sm.size() == 0);      // 但没有元素
  CHECK(sm.empty());
  CHECK(sm.begin() == sm.end());

  // 迭代必须不产出任何东西 —— 预填 dense_ 的写法会在这里挂
  int visited = 0;
  for (const int& v : sm) {
    (void)v;
    ++visited;
  }
  CHECK(visited == 0);
}

TEST_CASE("slotmap: reserve 后插入的元素仍然正确", "[core][slotmap]") {
  IntMap sm;
  sm.reserve(8);

  const auto a = sm.insert(10);
  const auto b = sm.insert(20);

  CHECK(sm.size() == 2);
  REQUIRE(sm.get(a) != nullptr);
  CHECK(*sm.get(a) == 10);
  REQUIRE(sm.get(b) != nullptr);
  CHECK(*sm.get(b) == 20);

  // reserve 之后 slot 仍然从 0 开始编号（没有被预填打乱）
  CHECK(a.index() == 0);
  CHECK(b.index() == 1);
  CHECK(a.generation() == 1);
}

// ============================================================================
// insert / get 基本语义
// ============================================================================

TEST_CASE("slotmap: 首个句柄是 index 0、generation 1", "[core][slotmap]") {
  IntMap sm;
  const auto h = sm.insert(42);

  CHECK(h.valid());
  CHECK(h.index() == 0);      // index 从 0 开始
  CHECK(h.generation() == 1); // generation 从 1 开始

  REQUIRE(sm.get(h) != nullptr);
  CHECK(*sm.get(h) == 42);
  CHECK(sm.contains(h));
}

TEST_CASE("slotmap: get 对无效句柄返回 nullptr", "[core][slotmap]") {
  IntMap sm;
  sm.insert(1);

  CHECK(sm.get(IntHandle{}) == nullptr);
  CHECK_FALSE(sm.contains(IntHandle{}));
}

TEST_CASE("slotmap: get 对越界 slot 返回 nullptr（不崩溃）", "[core][slotmap]") {
  IntMap sm;
  sm.insert(1);

  // 手工造一个 slot 号远超已创建 slot 数的句柄
  const IntHandle bogus = IntHandle::fromRaw((uint64_t{1} << 24) | 9999);

  CHECK(sm.get(bogus) == nullptr);
  CHECK_FALSE(sm.contains(bogus));
}

TEST_CASE("slotmap: operator[] 可读可写，与 get 一致", "[core][slotmap]") {
  IntMap sm;
  const auto h = sm.insert(1);

  CHECK(sm[h] == 1);
  sm[h] = 99; // 非 const 版本必须存在，否则这里编译不过
  CHECK(sm[h] == 99);
  REQUIRE(sm.get(h) != nullptr);
  CHECK(*sm.get(h) == 99);

  const IntMap& csm = sm; // 走 const 重载
  CHECK(csm[h] == 99);
  REQUIRE(csm.get(h) != nullptr);
  CHECK(*csm.get(h) == 99);
}

// ============================================================================
// erase：尾元素 / 唯一元素 / 中间元素
// ============================================================================

TEST_CASE("slotmap: erase 返回 false 表示句柄已失效", "[core][slotmap]") {
  IntMap sm;
  const auto h = sm.insert(1);

  CHECK(sm.erase(h));
  CHECK_FALSE(sm.erase(h));           // 再删一次：已失效
  CHECK_FALSE(sm.erase(IntHandle{})); // 无效句柄
}

TEST_CASE("slotmap: 删除尾元素后 get 返回 nullptr（swap-erase 自交换回归）", "[core][slotmap]") {
  IntMap sm;
  const auto a = sm.insert(1);
  const auto b = sm.insert(2);
  const auto c = sm.insert(3);

  REQUIRE(sm.erase(c)); // ← 删的正是尾巴：pos == last

  CHECK(sm.size() == 2);
  CHECK(sm.get(c) == nullptr); // 修复前这里会越界读到内存
  CHECK_FALSE(sm.contains(c));
  REQUIRE(sm.get(a) != nullptr);
  CHECK(*sm.get(a) == 1);
  REQUIRE(sm.get(b) != nullptr);
  CHECK(*sm.get(b) == 2);
}

TEST_CASE("slotmap: 删除唯一元素后容器为空且一切安全", "[core][slotmap]") {
  IntMap sm;
  const auto a = sm.insert(7);

  REQUIRE(sm.erase(a));

  CHECK(sm.empty());
  CHECK(sm.size() == 0);
  CHECK(sm.get(a) == nullptr);
  CHECK(sm.begin() == sm.end());
}

TEST_CASE("slotmap: 删除中间元素会把尾元素搬进空位", "[core][slotmap]") {
  IntMap sm;
  const auto a = sm.insert(1);
  const auto b = sm.insert(2);
  const auto c = sm.insert(3);

  REQUIRE(sm.erase(b)); // 中间元素

  CHECK(sm.size() == 2);
  CHECK(sm.get(b) == nullptr);
  REQUIRE(sm.get(a) != nullptr);
  CHECK(*sm.get(a) == 1); // a 不受影响
  REQUIRE(sm.get(c) != nullptr);
  CHECK(*sm.get(c) == 3); // c 被搬运后仍然可达

  // 稠密顺序：a 留在原位，c 被搬到 b 的位置
  const std::vector<int> got(sm.begin(), sm.end());
  REQUIRE(got.size() == 2);
  CHECK(got[0] == 1);
  CHECK(got[1] == 3);
}

TEST_CASE("slotmap: 反复删尾保持所有句柄有效", "[core][slotmap]") {
  IntMap sm;
  std::vector<IntHandle> hs;
  for (int i = 0; i < 5; ++i) {
    hs.push_back(sm.insert(i * 10));
  }

  for (auto it = hs.rbegin(); it != hs.rend(); ++it) {
    REQUIRE(sm.erase(*it));
  }

  CHECK(sm.empty());
  for (const auto& h : hs) {
    CHECK(sm.get(h) == nullptr);
  }
}

// ============================================================================
// slot 复用与 ABA
// ============================================================================

TEST_CASE("slotmap: slot 复用后旧句柄失效（ABA 核心）", "[core][slotmap]") {
  IntMap sm;
  const auto a = sm.insert(1);
  REQUIRE(sm.erase(a));

  const auto b = sm.insert(2);

  CHECK(a.index() == b.index()); // 复用了同一个 slot
  CHECK(a != b);                 // 但句柄不同
  CHECK(a.generation() != b.generation());
  CHECK(sm.get(a) == nullptr); // 旧句柄失效
  CHECK_FALSE(sm.contains(a));
  REQUIRE(sm.get(b) != nullptr);
  CHECK(*sm.get(b) == 2);
}

TEST_CASE("slotmap: 复用后 generation 递增", "[core][slotmap]") {
  IntMap sm;

  const auto a = sm.insert(1);
  CHECK(a.generation() == 1);

  REQUIRE(sm.erase(a));
  const auto b = sm.insert(2);
  CHECK(b.generation() == 2);

  REQUIRE(sm.erase(b));
  const auto c = sm.insert(3);
  CHECK(c.generation() == 3);
}

// ============================================================================
// clear
// ============================================================================

TEST_CASE("slotmap: clear 后旧句柄不会复活", "[core][slotmap]") {
  IntMap sm;
  const auto a = sm.insert(1);
  const auto b = sm.insert(2);

  sm.clear();

  CHECK(sm.empty());
  CHECK(sm.size() == 0);
  CHECK(sm.get(a) == nullptr);
  CHECK(sm.get(b) == nullptr);
  CHECK_FALSE(sm.contains(a));

  // 关键：clear 之后新插入的 handle 不能和旧的相等，
  // 否则旧句柄会"复活"并指向新对象（ABA 的另一种触发方式）
  const auto c = sm.insert(3);
  CHECK(c != a);
  CHECK(c != b);
  CHECK(sm.get(a) == nullptr); // a 仍然失效
  REQUIRE(sm.get(c) != nullptr);
  CHECK(*sm.get(c) == 3);
}

TEST_CASE("slotmap: clear 后可以继续正常插入多个元素", "[core][slotmap]") {
  IntMap sm;
  sm.insert(1);
  sm.insert(2);
  sm.clear();

  const auto a = sm.insert(10);
  const auto b = sm.insert(20);

  CHECK(sm.size() == 2);
  REQUIRE(sm.get(a) != nullptr);
  CHECK(*sm.get(a) == 10);
  REQUIRE(sm.get(b) != nullptr);
  CHECK(*sm.get(b) == 20);
}

// ============================================================================
// handleAt（遍历时反查句柄）
// ============================================================================

TEST_CASE("slotmap: handleAt 返回的句柄可用于 get", "[core][slotmap]") {
  IntMap sm;
  sm.insert(5);
  sm.insert(6);
  sm.insert(7);

  REQUIRE(sm.size() == 3);
  for (std::size_t pos = 0; pos < sm.size(); ++pos) {
    const IntHandle h = sm.handleAt(static_cast<uint32_t>(pos));
    CHECK(sm.contains(h));
    REQUIRE(sm.get(h) != nullptr);
    CHECK(*sm.get(h) == sm.data()[pos]); // 反查结果与 dense_ 顺序一致
  }
}

// ============================================================================
// 非 int 类型（证明不要求 T 可默认构造）
// ============================================================================

TEST_CASE("slotmap: 支持不可默认构造且不可平凡复制的类型", "[core][slotmap]") {
  struct NoDefault {
    explicit NoDefault(int v) : value(v) {}
    int value;
  };

  yr::core::SlotMap<NoDefault> sm;
  sm.reserve(4);

  const auto h = sm.insert(NoDefault{7});
  REQUIRE(sm.get(h) != nullptr);
  CHECK(sm.get(h)->value == 7);
}
