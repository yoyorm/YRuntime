#include <catch2/catch_test_macros.hpp>

#include <yr/core/assert.h>
#include <yr/core/string_id.h>
#include <yr/core/string_interner.h>
#include <yr/object/class_db.h>
#include <yr/object/class_info.h>
#include <yr/object/object.h>
#include <yr/object/register_core_classes.h>

#include <memory>

namespace {

  using yr::core::StringId;
  using yr::obj::ClassDB;
  using yr::obj::ClassInfo;
  using yr::obj::Object;

  StringId n(const char* text) {
    return yr::core::globalStringInterner().intern(text);
  }

  class Animal : public Object {};
  class Dog : public Animal {};

  const ClassInfo& animalInfo() {
    static const ClassInfo info(n("Animal"), nullptr, {});
    return info;
  }

  const ClassInfo& dogInfo() {
    static const ClassInfo info(
        n("Dog"), &animalInfo(), {}, +[]() -> Object* { return std::make_unique<Dog>().release(); });
    return info;
  }

  const ClassInfo& abstractInfo() {
    static const ClassInfo info(n("AbstractAnimal"), nullptr, {}); // 无工厂 = 抽象
    return info;
  }

#if YR_ENABLE_ASSERTS
  int g_assertCount = 0;
  yr::core::AssertAction countingHandler(const yr::core::AssertInfo&) noexcept {
    ++g_assertCount;
    return yr::core::AssertAction::kContinue;
  }
#endif

} // namespace

TEST_CASE("class db: 注册后可按名字查回", "[object][class_db]") {
  ClassDB db;
  db.registerClass(animalInfo());

  CHECK(db.getClass(n("Animal")) == &animalInfo());
  CHECK(db.getClass(n("Nope")) == nullptr);
}

TEST_CASE("class db: allClasses 列出所有已注册类", "[object][class_db]") {
  ClassDB db;
  db.registerClass(animalInfo());
  db.registerClass(dogInfo());

  const std::vector<StringId> all = db.allClasses();
  CHECK(all.size() == 2);
}

TEST_CASE("class db: inheritorsOf 返回继承者但不含自身", "[object][class_db]") {
  ClassDB db;
  db.registerClass(animalInfo());
  db.registerClass(dogInfo());

  const std::vector<StringId> inheritors = db.inheritorsOf(n("Animal"));
  REQUIRE(inheritors.size() == 1);
  CHECK(inheritors[0] == n("Dog"));

  CHECK(db.inheritorsOf(n("Dog")).empty());  // 不含自身
  CHECK(db.inheritorsOf(n("Nope")).empty()); // 未注册类
}

TEST_CASE("class db: instantiate 有工厂才创建，未注册/抽象返回 nullptr", "[object][class_db]") {
  ClassDB db;
  db.registerClass(dogInfo());
  db.registerClass(abstractInfo());

  const std::unique_ptr<Object> dog(db.instantiate(n("Dog")));
  REQUIRE(dog != nullptr);

  const std::unique_ptr<Object> unknown(db.instantiate(n("Nope")));
  CHECK(unknown == nullptr);

  const std::unique_ptr<Object> abstract(db.instantiate(n("AbstractAnimal")));
  CHECK(abstract == nullptr);
}

TEST_CASE("class db: 注册只在显式调用后发生（SIOF 保护）", "[object][class_db][siof]") {
  ClassDB& db = yr::obj::globalClassDB();

  // main 之前不应有任何注册：证明没有任何全局构造器偷偷注册
  CHECK(db.allClasses().empty());
  CHECK_FALSE(db.isFrozen());

  yr::obj::register_core_classes(); // 显式注册

  CHECK(db.getClass(n("Object")) != nullptr);
}

TEST_CASE("class db: freeze 之后注册触发断言", "[object][class_db][assert]") {
  ClassDB db;
  db.freeze();
  CHECK(db.isFrozen());

#if YR_ENABLE_ASSERTS
  g_assertCount = 0;
  yr::core::setAssertHandler(countingHandler);
#endif
  db.registerClass(animalInfo());
#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount == 1);
  yr::core::setAssertHandler(nullptr);
#endif
}

TEST_CASE("class db: 重复注册同名类触发断言", "[object][class_db][assert]") {
  ClassDB db;
  db.registerClass(animalInfo());

#if YR_ENABLE_ASSERTS
  g_assertCount = 0;
  yr::core::setAssertHandler(countingHandler);
#endif
  db.registerClass(animalInfo());
#if YR_ENABLE_ASSERTS
  CHECK(g_assertCount == 1);
  yr::core::setAssertHandler(nullptr);
#endif
}