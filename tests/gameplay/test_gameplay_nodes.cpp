#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <yr/core/string_interner.h>
#include <yr/gameplay/box_collider.h>
#include <yr/gameplay/counter.h>
#include <yr/gameplay/gameplay_classes.h>
#include <yr/gameplay/input_axis.h>
#include <yr/gameplay/key_trigger.h>
#include <yr/gameplay/move_motor.h>
#include <yr/input/input_state.h>
#include <yr/object/class_db.h>
#include <yr/scene/node.h>
#include <yr/scene/packed_scene.h>
#include <yr/scene/scene_reader.h>
#include <yr/scene/scene_tree.h>

namespace {

  using yr::core::StringId;
  using yr::gameplay::BoxCollider;
  using yr::gameplay::Counter;
  using yr::gameplay::InputAxis;
  using yr::gameplay::KeyTrigger;
  using yr::gameplay::MoveMotor;
  using yr::input::InputState;
  using yr::input::KeyEvent;
  using yr::input::KeyPhase;
  using yr::math::Vec3;
  using yr::scene::Node;
  using yr::scene::SceneTree;

  [[nodiscard]] StringId intern(const char* text) {
    return yr::core::globalStringInterner().intern(text);
  }

  [[nodiscard]] std::unique_ptr<Node> makeNode(const char* name) {
    auto node = std::make_unique<Node>();
    node->setName(name);
    return node;
  }

} // namespace

TEST_CASE("gameplay counter: methods, changed and reached", "[gameplay][counter]") {
  Counter source;
  Counter target;
  source.setStep(1);
  source.setTargetValue(2);
  target.setStep(10);
  source.connect(intern("reached"), target.id(), intern("increment"));

  source.increment();
  source.increment();
  CHECK(source.value() == 2);
  CHECK(target.value() == 10);

  source.increment();
  CHECK(source.value() == 3);
  CHECK(target.value() == 10); // reached 只发一次

  source.reset();
  CHECK(source.value() == 0);
  source.increment();
  source.increment();
  CHECK(target.value() == 20); // reset 后重新武装
}

TEST_CASE("gameplay input axis: reads InputState and normalizes diagonals", "[gameplay][input_axis]") {
  SceneTree tree;
  InputState input;
  tree.setInputState(&input);

  auto root = makeNode("Root");
  auto axis = std::make_unique<InputAxis>();
  axis->setName("Axis");
  axis->setPositiveX("key_d");
  axis->setPositiveZ("key_w");
  InputAxis* const raw = axis.get();
  REQUIRE(root->addChild(std::move(axis)));
  REQUIRE(tree.setRoot(std::move(root)));

  input.submit(KeyEvent{intern("key_d"), KeyPhase::kPressed, 1.0F});
  input.submit(KeyEvent{intern("key_w"), KeyPhase::kPressed, 1.0F});
  tree.process(0.016F);

  CHECK(raw->value().x == Catch::Approx(0.70710678F));
  CHECK(raw->value().z == Catch::Approx(0.70710678F));

  input.submit(KeyEvent{intern("key_d"), KeyPhase::kReleased, 0.0F});
  input.submit(KeyEvent{intern("key_w"), KeyPhase::kReleased, 0.0F});
  tree.process(0.016F);
  CHECK(raw->value() == Vec3{});
}

TEST_CASE("gameplay input axis drives move motor through a connection", "[gameplay][move_motor]") {
  SceneTree tree;
  InputState input;
  tree.setInputState(&input);

  auto root = makeNode("Root");
  auto player = makeNode("Player");
  auto axis = std::make_unique<InputAxis>();
  axis->setName("Input");
  axis->setPositiveX("key_d");
  auto motor = std::make_unique<MoveMotor>();
  motor->setName("Motor");
  motor->setSpeed(2.0);
  InputAxis* const rawAxis = axis.get();
  MoveMotor* const rawMotor = motor.get();
  REQUIRE(player->addChild(std::move(axis)));
  REQUIRE(player->addChild(std::move(motor)));
  rawAxis->connect(intern("axis_changed"), rawMotor->id(), intern("set_direction"));
  REQUIRE(root->addChild(std::move(player)));
  REQUIRE(tree.setRoot(std::move(root)));

  input.submit(KeyEvent{intern("key_d"), KeyPhase::kPressed, 1.0F});
  tree.process(0.5F);

  CHECK(rawAxis->value().x == 1.0F);
  CHECK(rawMotor->direction().x == 1.0F);
  CHECK(tree.root()->childAt(0)->position().x == Catch::Approx(1.0F));
}

TEST_CASE("gameplay move motor: solid collider blocks movement", "[gameplay][move_motor][collider]") {
  SceneTree tree;

  auto root = makeNode("Root");
  auto player = makeNode("Player");
  auto motor = std::make_unique<MoveMotor>();
  motor->setName("Motor");
  motor->setSpeed(1.0);
  auto body = std::make_unique<BoxCollider>();
  body->setName("Body");
  body->setHalfExtents(Vec3{0.5F, 0.5F, 0.5F});
  MoveMotor* const rawMotor = motor.get();
  REQUIRE(player->addChild(std::move(motor)));
  REQUIRE(player->addChild(std::move(body)));

  auto wall = makeNode("Wall");
  wall->setPosition(Vec3{0.5F, 0.0F, 0.0F});
  auto wallBody = std::make_unique<BoxCollider>();
  wallBody->setName("Body");
  wallBody->setHalfExtents(Vec3{0.5F, 0.5F, 0.5F});
  wallBody->setSolid(true);
  REQUIRE(wall->addChild(std::move(wallBody)));

  REQUIRE(root->addChild(std::move(player)));
  REQUIRE(root->addChild(std::move(wall)));
  REQUIRE(tree.setRoot(std::move(root)));
  tree.refreshTransforms();

  rawMotor->setDirection(Vec3{1.0F, 0.0F, 0.0F});
  tree.process(1.0F);

  CHECK(tree.root()->childAt(0)->position().x == 0.0F);
}

TEST_CASE("gameplay box collider: trigger emits entered and drives counter", "[gameplay][collider]") {
  SceneTree tree;

  auto root = makeNode("Root");
  auto player = makeNode("Player");
  auto playerBody = std::make_unique<BoxCollider>();
  playerBody->setName("Body");
  playerBody->setTag("player");
  playerBody->setHalfExtents(Vec3{0.5F, 0.5F, 0.5F});
  REQUIRE(player->addChild(std::move(playerBody)));

  auto pellet = makeNode("Pellet");
  pellet->setPosition(Vec3{0.25F, 0.0F, 0.0F});
  auto sensor = std::make_unique<BoxCollider>();
  sensor->setName("Sensor");
  sensor->setTrigger(true);
  sensor->setFilterTag("player");
  sensor->setHalfExtents(Vec3{0.5F, 0.5F, 0.5F});
  BoxCollider* const rawSensor = sensor.get();
  REQUIRE(pellet->addChild(std::move(sensor)));

  auto score = std::make_unique<Counter>();
  score->setName("Score");
  Counter* const rawScore = score.get();

  REQUIRE(root->addChild(std::move(player)));
  REQUIRE(root->addChild(std::move(pellet)));
  REQUIRE(root->addChild(std::move(score)));
  rawSensor->connect(intern("entered"), rawScore->id(), intern("increment"));
  REQUIRE(tree.setRoot(std::move(root)));
  tree.refreshTransforms();

  tree.process(0.016F);
  CHECK(rawScore->value() == 1);

  tree.process(0.016F);
  CHECK(rawScore->value() == 1); // entered 只在重叠变化时发
}

TEST_CASE("gameplay key trigger: pressed edge drives counter", "[gameplay][key_trigger]") {
  SceneTree tree;
  InputState input;
  tree.setInputState(&input);

  auto root = makeNode("Root");
  auto trigger = std::make_unique<KeyTrigger>();
  trigger->setName("Trigger");
  trigger->setKey("key_enter");
  trigger->setPhase("pressed");
  KeyTrigger* const rawTrigger = trigger.get();
  auto counter = std::make_unique<Counter>();
  counter->setName("Counter");
  Counter* const rawCounter = counter.get();
  REQUIRE(root->addChild(std::move(trigger)));
  REQUIRE(root->addChild(std::move(counter)));
  rawTrigger->connect(intern("triggered"), rawCounter->id(), intern("increment"));
  REQUIRE(tree.setRoot(std::move(root)));

  input.submit(KeyEvent{intern("key_enter"), KeyPhase::kPressed, 1.0F});
  tree.process(0.016F);
  CHECK(rawCounter->value() == 1);
}

TEST_CASE("gameplay move motor: nested actor collides in world space", "[gameplay][move_motor][collider]") {
  SceneTree tree;

  auto root = makeNode("Root");
  auto container = makeNode("Container");
  container->setPosition(Vec3{10.0F, 0.0F, 0.0F});
  auto player = makeNode("Player");
  auto motor = std::make_unique<MoveMotor>();
  motor->setName("Motor");
  motor->setSpeed(1.0);
  auto body = std::make_unique<BoxCollider>();
  body->setName("Body");
  body->setHalfExtents(Vec3{0.5F, 0.5F, 0.5F});
  MoveMotor* const rawMotor = motor.get();
  REQUIRE(player->addChild(std::move(motor)));
  REQUIRE(player->addChild(std::move(body)));
  REQUIRE(container->addChild(std::move(player)));

  auto wall = makeNode("Wall");
  wall->setPosition(Vec3{10.5F, 0.0F, 0.0F});
  auto wallBody = std::make_unique<BoxCollider>();
  wallBody->setName("Body");
  wallBody->setHalfExtents(Vec3{0.5F, 0.5F, 0.5F});
  wallBody->setSolid(true);
  REQUIRE(wall->addChild(std::move(wallBody)));

  REQUIRE(root->addChild(std::move(container)));
  REQUIRE(root->addChild(std::move(wall)));
  REQUIRE(tree.setRoot(std::move(root)));
  tree.refreshTransforms();

  rawMotor->setDirection(Vec3{1.0F, 0.0F, 0.0F});
  tree.process(1.0F);

  CHECK(tree.root()->childAt(0)->childAt(0)->position().x == 0.0F);
}

namespace {

  using yr::obj::ClassDB;

  [[nodiscard]] const ClassDB& gameplayClasses() {
    yr::gameplay::register_gameplay_classes();
    return yr::obj::globalClassDB();
  }

  [[nodiscard]] yr::scene::Node* findByName(yr::scene::Node& node, std::string_view name) {
    if (node.name().view() == name) {
      return &node;
    }
    for (std::size_t index = 0; index < node.childCount(); ++index) {
      if (yr::scene::Node* hit = findByName(*node.childAt(index), name); hit != nullptr) {
        return hit;
      }
    }
    return nullptr;
  }

  constexpr std::string_view kGameplayScene =
      "[scene]\n"
      "\n"
      "[node name=\"Root\" type=\"Node\"]\n"
      "\n"
      "[node name=\"Player\" type=\"Node\" parent=\".\"]\n"
      "\n"
      "[node name=\"Input\" type=\"InputAxis\" parent=\"Player\"]\n"
      "positive_x = \"key_d\"\n"
      "\n"
      "[node name=\"Motor\" type=\"MoveMotor\" parent=\"Player\"]\n"
      "speed = 3.0\n"
      "\n"
      "[node name=\"Body\" type=\"BoxCollider\" parent=\"Player\"]\n"
      "half_extents = Vector3(0.4, 0.4, 0.4)\n"
      "tag = \"player\"\n"
      "\n"
      "[node name=\"Pellet\" type=\"Node\" parent=\".\"]\n"
      "position = Vector3(0.5, 0, 0)\n"
      "\n"
      "[node name=\"Sensor\" type=\"BoxCollider\" parent=\"Pellet\"]\n"
      "half_extents = Vector3(0.5, 0.5, 0.5)\n"
      "trigger = true\n"
      "filter_tag = \"player\"\n"
      "\n"
      "[node name=\"Score\" type=\"Counter\" parent=\".\"]\n"
      "step = 1\n"
      "target_value = 3\n"
      "\n"
      "[connection from=\"Player/Input\" to=\"Player/Motor\" signal=\"axis_changed\" method=\"set_direction\"]\n"
      "[connection from=\"Pellet/Sensor\" to=\"Score\" signal=\"entered\" method=\"increment\"]\n";

} // namespace

TEST_CASE("gameplay scene: text scene wires input, sensor and counter", "[gameplay][scene]") {
  const yr::scene::SceneReader reader(gameplayClasses());
  const yr::scene::SceneLoadResult result = reader.read(kGameplayScene);
  REQUIRE(result.ok());

  std::unique_ptr<yr::scene::Node> root = result.scene.instantiate(gameplayClasses());
  REQUIRE(root != nullptr);

  SceneTree tree;
  InputState input;
  tree.setInputState(&input);
  REQUIRE(tree.setRoot(std::move(root)));
  tree.refreshTransforms();

  auto* const score = dynamic_cast<Counter*>(findByName(*tree.root(), "Score"));
  auto* const player = findByName(*tree.root(), "Player");
  REQUIRE(score != nullptr);
  REQUIRE(player != nullptr);

  tree.process(0.016F);
  CHECK(score->value() == 1);
  tree.process(0.016F);
  CHECK(score->value() == 1); // entered 只在进入时发一次

  input.submit(KeyEvent{intern("key_d"), KeyPhase::kPressed, 1.0F});
  tree.process(0.1F);
  CHECK(player->position().x == Catch::Approx(0.3F));
}

TEST_CASE("gameplay box collider: disabling a sensor emits exited", "[gameplay][collider]") {
  SceneTree tree;

  auto root = makeNode("Root");
  auto player = makeNode("Player");
  auto playerBody = std::make_unique<BoxCollider>();
  playerBody->setName("Body");
  playerBody->setTag("player");
  playerBody->setHalfExtents(Vec3{0.5F, 0.5F, 0.5F});
  REQUIRE(player->addChild(std::move(playerBody)));

  auto pellet = makeNode("Pellet");
  auto sensor = std::make_unique<BoxCollider>();
  sensor->setName("Sensor");
  sensor->setTrigger(true);
  sensor->setFilterTag("player");
  sensor->setHalfExtents(Vec3{0.5F, 0.5F, 0.5F});
  BoxCollider* const rawSensor = sensor.get();
  REQUIRE(pellet->addChild(std::move(sensor)));

  auto enteredCounter = std::make_unique<Counter>();
  enteredCounter->setName("Entered");
  Counter* const entered = enteredCounter.get();
  auto exitedCounter = std::make_unique<Counter>();
  exitedCounter->setName("Exited");
  Counter* const exited = exitedCounter.get();
  REQUIRE(root->addChild(std::move(player)));
  REQUIRE(root->addChild(std::move(pellet)));
  REQUIRE(root->addChild(std::move(enteredCounter)));
  REQUIRE(root->addChild(std::move(exitedCounter)));
  rawSensor->connect(intern("entered"), entered->id(), intern("increment"));
  rawSensor->connect(intern("exited"), exited->id(), intern("increment"));
  REQUIRE(tree.setRoot(std::move(root)));
  tree.refreshTransforms();

  tree.process(0.016F);
  CHECK(entered->value() == 1);
  CHECK(exited->value() == 0);

  rawSensor->setEnabled(false);
  tree.process(0.016F);
  CHECK(exited->value() == 1);

  tree.process(0.016F);
  CHECK(exited->value() == 1); // 已清空，不重复发
}

TEST_CASE("gameplay key trigger: unknown phase is rejected", "[gameplay][key_trigger]") {
  KeyTrigger trigger;
  CHECK_FALSE(trigger.setPhase("press"));
  CHECK(trigger.phase() == KeyPhase::kPressed);
  CHECK(trigger.setPhase("released"));
  CHECK(trigger.phase() == KeyPhase::kReleased);
}
