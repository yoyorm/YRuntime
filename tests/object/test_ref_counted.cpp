#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

#include <yr/object/ref.h>
#include <yr/object/ref_counted.h>

namespace {

  // 隐藏 tag [.]：默认 ctest 不跑；用户实现后显式运行 "[todo]"。
  constexpr const char* kTodoTags = "[object][ref][.][todo]";

  class Probe : public yr::obj::RefCounted {
  public:
    static inline int alive = 0;

    Probe() { ++alive; }
    explicit Probe(int value) : value_(value) { ++alive; }
    ~Probe() override { --alive; }

    [[nodiscard]] int value() const noexcept { return value_; }

  private:
    int value_ = 0;
  };

  class DerivedProbe final : public Probe {
  public:
    using Probe::Probe;
    ~DerivedProbe() override = default;
  };

} // namespace

TEST_CASE("ref_counted: fresh object starts at zero", kTodoTags) {
  Probe probe;
  CHECK(probe.refcount() == 0);
}

TEST_CASE("ref_counted: ref increments and unref reports zero", kTodoTags) {
  // unique_ptr 只负责最终回收；这里只验证 unref() 的返回值语义，不按契约 delete。
  const std::unique_ptr<Probe> probe = std::make_unique<Probe>();
  probe->ref();
  CHECK(probe->refcount() == 1);
  probe->ref();
  CHECK(probe->refcount() == 2);

  CHECK_FALSE(probe->unref());
  CHECK(probe->refcount() == 1);
  CHECK(probe->unref());
  CHECK(probe->refcount() == 0);
}

TEST_CASE("ref: scope destroys the owned object", kTodoTags) {
  const int baseline = Probe::alive;
  {
    yr::obj::Ref<Probe> ref = yr::obj::make_ref<Probe>();
    CHECK(Probe::alive == baseline + 1);
    REQUIRE(ref.valid());
    CHECK(ref->refcount() == 1);
  }
  CHECK(Probe::alive == baseline);
}

TEST_CASE("ref: copy increments and move steals without changing count", kTodoTags) {
  yr::obj::Ref<Probe> first = yr::obj::make_ref<Probe>();
  Probe* const raw = first.get();
  REQUIRE(raw != nullptr);
  REQUIRE(raw->refcount() == 1);

  yr::obj::Ref<Probe> copy = first;
  CHECK(copy.get() == raw);
  CHECK(raw->refcount() == 2);

  yr::obj::Ref<Probe> moved = std::move(copy);
  CHECK_FALSE(copy.valid());
  CHECK(moved.get() == raw);
  CHECK(raw->refcount() == 2);
}

TEST_CASE("ref: reset releases and self assignment is safe", kTodoTags) {
  const int baseline = Probe::alive;
  {
    yr::obj::Ref<Probe> ref = yr::obj::make_ref<Probe>();
    Probe* const raw = ref.get();
    REQUIRE(raw != nullptr);
    REQUIRE(raw->refcount() == 1);

    yr::obj::Ref<Probe>& alias = ref;
    ref = alias;
    CHECK(ref.get() == raw);
    CHECK(raw->refcount() == 1);

    ref.reset();
    CHECK_FALSE(ref.valid());
    CHECK(Probe::alive == baseline);
  }
  CHECK(Probe::alive == baseline);
}

TEST_CASE("ref: base ref owns derived object with virtual destruction", kTodoTags) {
  const int baseline = Probe::alive;
  {
    yr::obj::Ref<Probe> base = yr::obj::make_ref<DerivedProbe>(42);
    REQUIRE(base.valid());
    CHECK(base->value() == 42);
    CHECK(base->refcount() == 1);
  }
  CHECK(Probe::alive == baseline);
}

TEST_CASE("ref: make_ref forwards constructor arguments", kTodoTags) {
  yr::obj::Ref<Probe> ref = yr::obj::make_ref<Probe>(123);
  REQUIRE(ref.valid());
  CHECK(ref->value() == 123);
}

// 循环引用不会被 RefCounted 自动回收；M9-0 不实现 WeakRef。
// 不写会泄漏的测试，避免在默认/隐藏测试里留下无法清理的 ObjectDB 条目。