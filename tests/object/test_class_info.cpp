#include <catch2/catch_test_macros.hpp>

#include <yr/core/string_id.h>
#include <yr/core/string_interner.h>
#include <yr/object/class_info.h>
#include <yr/object/object.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/object/vec3.h>

#include <cstdint>
#include <string>
#include <utility>

namespace {

  using yr::core::StringId;
  using yr::obj::Array;
  using yr::obj::ClassInfo;
  using yr::obj::Dict;
  using yr::obj::Object;
  using yr::obj::PropertyInfo;
  using yr::obj::Variant;
  using yr::obj::Vec3;

  StringId nameOf(const char* text) {
    return yr::core::globalStringInterner().intern(text);
  }

  // ---- 被反射的类（手工写，不用宏）----
  class Player : public Object {
  public:
    std::int64_t hp = 100;
    double speed = 3.5;
    std::string name = "hero";
    Vec3 position{1.0F, 2.0F, 3.0F};
    yr::core::ObjectID target;
    Array inventory;
    Dict stats;
    std::int64_t level = 1;
    std::int64_t secret = 0;
  };

  class Warrior : public Player {
  public:
    std::int64_t rage = 0;
  };

  // ---- 手工注册：属性表 + ClassInfo（用函数内 static，避开 SIOF）----
  const ClassInfo& playerClass() {
    static const PropertyInfo props[] = {
        {.name = nameOf("hp"),
         .type = Variant::Type::kInt,
         .flags = yr::obj::kEditable | yr::obj::kSerialized,
         .getter = +[](const Object* o) -> Variant { return Variant(static_cast<const Player*>(o)->hp); },
         .setter = +[](Object* o, const Variant& v) -> bool {
           const auto value = v.tryAs<std::int64_t>();
           if (!value) {
             return false;
           }
           static_cast<Player*>(o)->hp = *value;
           return true;
         }},
        {.name = nameOf("speed"),
         .type = Variant::Type::kFloat,
         .flags = yr::obj::kEditable,
         .getter = +[](const Object* o) -> Variant { return Variant(static_cast<const Player*>(o)->speed); },
         .setter = +[](Object* o, const Variant& v) -> bool {
           const auto value = v.tryAs<double>();
           if (!value) {
             return false;
           }
           static_cast<Player*>(o)->speed = *value;
           return true;
         }},
        {.name = nameOf("name"),
         .type = Variant::Type::kString,
         .flags = yr::obj::kEditable,
         .getter = +[](const Object* o) -> Variant { return Variant(static_cast<const Player*>(o)->name); },
         .setter = +[](Object* o, const Variant& v) -> bool {
           auto value = v.tryAs<std::string>();
           if (!value) {
             return false;
           }
           static_cast<Player*>(o)->name = std::move(*value);
           return true;
         }},
        {.name = nameOf("position"),
         .type = Variant::Type::kVec3,
         .flags = yr::obj::kEditable,
         .getter = +[](const Object* o) -> Variant { return Variant(static_cast<const Player*>(o)->position); },
         .setter = +[](Object* o, const Variant& v) -> bool {
           const auto value = v.tryAs<Vec3>();
           if (!value) {
             return false;
           }
           static_cast<Player*>(o)->position = *value;
           return true;
         }},
        {.name = nameOf("target"),
         .type = Variant::Type::kObjectID,
         .flags = yr::obj::kEditable,
         .getter = +[](const Object* o) -> Variant { return Variant(static_cast<const Player*>(o)->target); },
         .setter = +[](Object* o, const Variant& v) -> bool {
           const auto value = v.tryAs<yr::core::ObjectID>();
           if (!value) {
             return false;
           }
           static_cast<Player*>(o)->target = *value;
           return true;
         }},
        {.name = nameOf("inventory"),
         .type = Variant::Type::kArray,
         .flags = yr::obj::kEditable,
         .getter = +[](const Object* o) -> Variant { return Variant(static_cast<const Player*>(o)->inventory); },
         .setter = +[](Object* o, const Variant& v) -> bool {
           auto value = v.tryAs<Array>();
           if (!value) {
             return false;
           }
           static_cast<Player*>(o)->inventory = std::move(*value);
           return true;
         }},
        {.name = nameOf("stats"),
         .type = Variant::Type::kDict,
         .flags = yr::obj::kEditable,
         .getter = +[](const Object* o) -> Variant { return Variant(static_cast<const Player*>(o)->stats); },
         .setter = +[](Object* o, const Variant& v) -> bool {
           auto value = v.tryAs<Dict>();
           if (!value) {
             return false;
           }
           static_cast<Player*>(o)->stats = std::move(*value);
           return true;
         }},
        // 只读：有 setter，但 flag 是 kReadOnly → ClassInfo::set_property 必须拒绝
        {.name = nameOf("level"),
         .type = Variant::Type::kInt,
         .flags = yr::obj::kReadOnly,
         .getter = +[](const Object* o) -> Variant { return Variant(static_cast<const Player*>(o)->level); },
         .setter = +[](Object* o, const Variant& v) -> bool {
           const auto value = v.tryAs<std::int64_t>();
           if (!value) {
             return false;
           }
           static_cast<Player*>(o)->level = *value;
           return true;
         }},
        // 只写：没有 getter
        {.name = nameOf("secret"),
         .type = Variant::Type::kInt,
         .flags = yr::obj::kEditable,
         .setter = +[](Object* o, const Variant& v) -> bool {
           const auto value = v.tryAs<std::int64_t>();
           if (!value) {
             return false;
           }
           static_cast<Player*>(o)->secret = *value;
           return true;
         }},
    };
    static const ClassInfo info(nameOf("Player"), nullptr, props);
    return info;
  }

  const ClassInfo& warriorClass() {
    static const PropertyInfo props[] = {
        {.name = nameOf("rage"),
         .type = Variant::Type::kInt,
         .flags = yr::obj::kEditable,
         .getter = +[](const Object* o) -> Variant { return Variant(static_cast<const Warrior*>(o)->rage); },
         .setter = +[](Object* o, const Variant& v) -> bool {
           const auto value = v.tryAs<std::int64_t>();
           if (!value) {
             return false;
           }
           static_cast<Warrior*>(o)->rage = *value;
           return true;
         }},
        // 遮蔽：与 Player::hp 同名，返回 hp*2，用来验证「派生优先」
        {.name = nameOf("hp"),
         .type = Variant::Type::kInt,
         .flags = yr::obj::kReadOnly,
         .getter = +[](const Object* o) -> Variant { return Variant(static_cast<const Warrior*>(o)->hp * 2); }},
    };
    static const ClassInfo info(nameOf("Warrior"), &playerClass(), props);
    return info;
  }

} // namespace

TEST_CASE("class info: 每种类型 get/set round-trip", "[object][class_info]") {
  const ClassInfo& klass = playerClass();
  Player player;
  Variant out;

  // int
  REQUIRE(klass.get_property(&player, nameOf("hp"), out));
  CHECK(out.as<std::int64_t>() == 100);
  REQUIRE(klass.set_property(&player, nameOf("hp"), Variant(std::int64_t{50})));
  CHECK(player.hp == 50);

  // float
  REQUIRE(klass.set_property(&player, nameOf("speed"), Variant(9.5)));
  CHECK(player.speed == 9.5);

  // string
  REQUIRE(klass.set_property(&player, nameOf("name"), Variant(std::string("orc"))));
  CHECK(player.name == "orc");
  REQUIRE(klass.get_property(&player, nameOf("name"), out));
  CHECK(out.as<std::string>() == "orc");

  // vec3
  REQUIRE(klass.set_property(&player, nameOf("position"), Variant(Vec3(4.0F, 5.0F, 6.0F))));
  CHECK(player.position == Vec3(4.0F, 5.0F, 6.0F));

  // objectid
  const yr::core::ObjectID id = player.id();
  REQUIRE(klass.set_property(&player, nameOf("target"), Variant(id)));
  CHECK(player.target == id);

  // array
  Array inv{Variant(std::int64_t{1}), Variant(std::string("sword"))};
  REQUIRE(klass.set_property(&player, nameOf("inventory"), Variant(inv)));
  CHECK(player.inventory.size() == 2);
  REQUIRE(klass.get_property(&player, nameOf("inventory"), out));
  CHECK(out.as<Array>().size() == 2);

  // dict
  Dict stats;
  stats.emplace_back(nameOf("atk"), Variant(std::int64_t{7}));
  REQUIRE(klass.set_property(&player, nameOf("stats"), Variant(stats)));
  CHECK(player.stats.size() == 1);
  REQUIRE(klass.get_property(&player, nameOf("stats"), out));
  CHECK(out.as<Dict>()[0].first == nameOf("atk"));
}

TEST_CASE("class info: 未知属性读写都返回 false", "[object][class_info]") {
  const ClassInfo& klass = playerClass();
  Player player;
  Variant out;

  CHECK_FALSE(klass.get_property(&player, nameOf("no_such_prop"), out));
  CHECK_FALSE(klass.set_property(&player, nameOf("no_such_prop"), Variant(std::int64_t{1})));
}

TEST_CASE("class info: set 类型不符返回 false 且不写入", "[object][class_info]") {
  const ClassInfo& klass = playerClass();
  Player player;
  const std::int64_t before = player.hp;

  CHECK_FALSE(klass.set_property(&player, nameOf("hp"), Variant(std::string("not an int"))));
  CHECK(player.hp == before);
}

TEST_CASE("class info: 只读属性 set 返回 false", "[object][class_info]") {
  const ClassInfo& klass = playerClass();
  Player player;
  const std::int64_t before = player.level;

  CHECK_FALSE(klass.set_property(&player, nameOf("level"), Variant(std::int64_t{99})));
  CHECK(player.level == before);
}

TEST_CASE("class info: 只写属性 get 返回 false", "[object][class_info]") {
  const ClassInfo& klass = playerClass();
  Player player;
  Variant out;

  REQUIRE(klass.set_property(&player, nameOf("secret"), Variant(std::int64_t{42})));
  CHECK(player.secret == 42);
  CHECK_FALSE(klass.get_property(&player, nameOf("secret"), out));
}

TEST_CASE("class info: 继承属性在派生类可见", "[object][class_info]") {
  const ClassInfo& klass = warriorClass();
  Warrior warrior;
  Variant out;

  CHECK(klass.get_property(&warrior, nameOf("name"), out)); // 来自 Player
  CHECK(out.as<std::string>() == "hero");

  REQUIRE(klass.set_property(&warrior, nameOf("rage"), Variant(std::int64_t{5})));
  CHECK(warrior.rage == 5);
}

TEST_CASE("class info: 派生类遮蔽基类属性", "[object][class_info]") {
  Warrior warrior;
  warrior.hp = 10;

  const PropertyInfo* derived = warriorClass().findProperty(nameOf("hp"));
  const PropertyInfo* base = playerClass().findProperty(nameOf("hp"));
  REQUIRE(derived != nullptr);
  REQUIRE(base != nullptr);
  CHECK(derived != base); // 是两个不同的属性行

  Variant out;
  REQUIRE(warriorClass().get_property(&warrior, nameOf("hp"), out));
  CHECK(out.as<std::int64_t>() == 20); // 遮蔽版返回 hp*2
  REQUIRE(playerClass().get_property(&warrior, nameOf("hp"), out));
  CHECK(out.as<std::int64_t>() == 10); // 基类版返回原值
}

TEST_CASE("class info: isA 沿继承链判断", "[object][class_info]") {
  const ClassInfo& player = playerClass();
  const ClassInfo& warrior = warriorClass();

  CHECK(warrior.isA(&warrior));
  CHECK(warrior.isA(&player)); // Warrior 派生自 Player
  CHECK_FALSE(player.isA(&warrior));
  CHECK_FALSE(player.isA(nullptr));
}