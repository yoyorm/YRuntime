#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include <yr/core/string_interner.h>
#include <yr/math/vec3.h>
#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/camera_node.h>

namespace {

  using yr::core::StringId;
  using yr::math::Vec3;
  using yr::obj::Variant;
  using yr::scene::CameraNode;

  [[nodiscard]] StringId intern(const char* text) {
    return yr::core::globalStringInterner().intern(text);
  }

} // namespace

TEST_CASE("CameraNode: defaults look down -Z with a 60 degree fov", "[scene][camera]") {
  CameraNode camera;
  CHECK(camera.yawDegrees() == 0.0F);
  CHECK(camera.pitchDegrees() == 0.0F);
  CHECK(camera.fovDegrees() == 60.0F);
  CHECK(camera.nearPlane() == 0.1F);
  CHECK(camera.farPlane() == 1000.0F);

  const Vec3 forward = camera.forward();
  CHECK(forward.x == Catch::Approx(0.0F));
  CHECK(forward.y == Catch::Approx(0.0F));
  CHECK(forward.z == Catch::Approx(-1.0F));
}

TEST_CASE("CameraNode: yaw turns right and pitch is clamped", "[scene][camera]") {
  CameraNode camera;

  camera.setYawDegrees(90.0F);
  Vec3 forward = camera.forward();
  CHECK(forward.x == Catch::Approx(1.0F));
  CHECK(forward.y == Catch::Approx(0.0F));
  CHECK(forward.z == Catch::Approx(0.0F).margin(1e-5F));

  camera.setYawDegrees(180.0F);
  forward = camera.forward();
  CHECK(forward.z == Catch::Approx(1.0F));

  camera.setPitchDegrees(89.0F);
  CHECK(camera.pitchDegrees() == 89.0F);
  camera.setPitchDegrees(120.0F);
  CHECK(camera.pitchDegrees() == 89.0F);
  camera.setPitchDegrees(-120.0F);
  CHECK(camera.pitchDegrees() == -89.0F);
}

TEST_CASE("CameraNode: look adds mouse deltas in degrees", "[scene][camera]") {
  CameraNode camera;
  camera.look(Vec3{10.0F, -20.0F, 0.0F});
  CHECK(camera.yawDegrees() == 10.0F);
  CHECK(camera.pitchDegrees() == -20.0F);

  camera.look(Vec3{355.0F, 0.0F, 0.0F}); // 365 -> 5
  CHECK(camera.yawDegrees() == Catch::Approx(5.0F));
}

TEST_CASE("CameraNode: ClassInfo exposes serializable camera properties", "[scene][camera][property]") {
  CameraNode camera;
  const yr::obj::ClassInfo& info = yr::scene::cameraNodeClassInfo();

  CHECK(info.set_property(&camera, intern("yaw_degrees"), Variant(45.0)));
  CHECK(info.set_property(&camera, intern("pitch_degrees"), Variant(-10.0)));
  CHECK(info.set_property(&camera, intern("fov_degrees"), Variant(75.0)));
  CHECK(info.set_property(&camera, intern("near_plane"), Variant(0.5)));
  CHECK(info.set_property(&camera, intern("far_plane"), Variant(300.0)));

  CHECK(camera.yawDegrees() == 45.0F);
  CHECK(camera.pitchDegrees() == -10.0F);
  CHECK(camera.fovDegrees() == 75.0F);
  CHECK(camera.nearPlane() == 0.5F);
  CHECK(camera.farPlane() == 300.0F);

  Variant out;
  REQUIRE(info.get_property(&camera, intern("yaw_degrees"), out));
  CHECK(out.tryAs<double>().value_or(0.0) == Catch::Approx(45.0));
}

TEST_CASE("CameraNode: look method is reachable through ClassInfo", "[scene][camera][property]") {
  CameraNode camera;
  const yr::obj::ClassInfo& info = yr::scene::cameraNodeClassInfo();
  REQUIRE(info.invoke(&camera, intern("look"), Variant(Vec3{30.0F, 15.0F, 0.0F})));
  CHECK(camera.yawDegrees() == 30.0F);
  CHECK(camera.pitchDegrees() == 15.0F);
}
