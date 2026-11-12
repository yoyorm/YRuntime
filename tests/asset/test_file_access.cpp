#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <yr/asset/disk_file_access.h>
#include <yr/asset/file_access.h>
#include <yr/asset/memory_file_access.h>
#include <yr/asset/throttled_file_access.h>

namespace {

  using yr::asset::DiskFileAccess;
  using yr::asset::IFileAccess;
  using yr::asset::MemoryFileAccess;
  using yr::asset::ThrottledFileAccess;

  std::vector<std::uint8_t> bytesOf(std::string_view text) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size());
    for (const char c : text) {
      bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return bytes;
  }

} // namespace

TEST_CASE("file access: memory add and read", "[asset][file_access]") {
  MemoryFileAccess access;
  const std::vector<std::uint8_t> payload{0x00, 0x01, 0x7F, 0xAB, 0xFF};
  access.addFile("res://data/blob.bin", payload);

  std::vector<std::uint8_t> out;
  std::string error;
  REQUIRE(access.readFile("res://data/blob.bin", out, error));
  CHECK(error.empty());
  CHECK(out == payload);
}

TEST_CASE("file access: memory missing path", "[asset][file_access]") {
  MemoryFileAccess access;
  access.addFile("res://data/other.bin", bytesOf("other"));

  std::vector<std::uint8_t> out{0xDE, 0xAD};
  std::string error = "stale";
  CHECK_FALSE(access.readFile("res://data/missing.bin", out, error));
  CHECK_FALSE(error.empty());
  CHECK(out.empty());
}

TEST_CASE("file access: memory add overwrites same path", "[asset][file_access]") {
  MemoryFileAccess access;
  access.addFile("res://data/blob.bin", bytesOf("first"));
  access.addFile("res://data/blob.bin", bytesOf("second"));

  std::vector<std::uint8_t> out;
  std::string error;
  REQUIRE(access.readFile("res://data/blob.bin", out, error));
  CHECK(out == bytesOf("second"));
}

TEST_CASE("file access: memory clear removes all files", "[asset][file_access]") {
  MemoryFileAccess access;
  access.addFile("res://data/blob.bin", bytesOf("payload"));

  std::vector<std::uint8_t> out;
  std::string error;
  REQUIRE(access.readFile("res://data/blob.bin", out, error));

  access.clear();
  CHECK_FALSE(access.readFile("res://data/blob.bin", out, error));
  CHECK_FALSE(error.empty());
  CHECK(out.empty());
}

TEST_CASE("file access: disk reads fixture file", "[asset][file_access]") {
  DiskFileAccess access(YR_TEST_FIXTURE_ROOT);

  std::vector<std::uint8_t> out;
  std::string error;
  REQUIRE(access.readFile("res://triangle.gltf", out, error));
  CHECK(error.empty());
  CHECK_FALSE(out.empty());

  std::ifstream file(std::string(YR_TEST_FIXTURE_ROOT) + "/triangle.gltf", std::ios::binary);
  REQUIRE(file);
  const std::string expected((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  CHECK(out == bytesOf(expected));
}

TEST_CASE("file access: disk missing file", "[asset][file_access]") {
  DiskFileAccess access(YR_TEST_FIXTURE_ROOT);

  std::vector<std::uint8_t> out{0xDE, 0xAD};
  std::string error = "stale";
  CHECK_FALSE(access.readFile("res://no_such_file.gltf", out, error));
  CHECK_FALSE(error.empty());
  CHECK(out.empty());
}

TEST_CASE("file access: disk rejects non-canonical path", "[asset][file_access]") {
  DiskFileAccess access(YR_TEST_FIXTURE_ROOT);

  std::vector<std::uint8_t> out{0xDE, 0xAD};
  std::string error = "stale";
  CHECK_FALSE(access.readFile("res://../triangle.gltf", out, error));
  CHECK_FALSE(error.empty());
  CHECK(out.empty());
}

TEST_CASE("file access: throttled forwards reads to inner", "[asset][file_access]") {
  auto memory = std::make_unique<MemoryFileAccess>();
  memory->addFile("res://data/blob.bin", bytesOf("throttled"));
  ThrottledFileAccess access(std::move(memory), std::chrono::milliseconds(0));

  std::vector<std::uint8_t> out;
  std::string error;
  REQUIRE(access.readFile("res://data/blob.bin", out, error));
  CHECK(error.empty());
  CHECK(out == bytesOf("throttled"));
}

TEST_CASE("file access: throttled passes missing path error through", "[asset][file_access]") {
  auto memory = std::make_unique<MemoryFileAccess>();
  ThrottledFileAccess access(std::move(memory), std::chrono::milliseconds(0));

  MemoryFileAccess reference;
  std::vector<std::uint8_t> expectedOut;
  std::string expectedError;
  REQUIRE_FALSE(reference.readFile("res://data/missing.bin", expectedOut, expectedError));

  std::vector<std::uint8_t> out{0xDE, 0xAD};
  std::string error;
  CHECK_FALSE(access.readFile("res://data/missing.bin", out, error));
  CHECK(error == expectedError);
  CHECK(out.empty());
}

TEST_CASE("file access: throttled waits on every read", "[asset][file_access]") {
  auto memory = std::make_unique<MemoryFileAccess>();
  memory->addFile("res://data/blob.bin", bytesOf("payload"));
  ThrottledFileAccess access(std::move(memory), std::chrono::milliseconds(10));

  std::vector<std::uint8_t> out;
  std::string error;

  for (int read = 0; read < 2; ++read) {
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(access.readFile("res://data/blob.bin", out, error));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed >= std::chrono::milliseconds(5));
  }
}

TEST_CASE("file access: throttled without inner fails", "[asset][file_access]") {
  ThrottledFileAccess access(nullptr, std::chrono::milliseconds(0));

  std::vector<std::uint8_t> out{0xDE, 0xAD};
  std::string error = "stale";
  CHECK_FALSE(access.readFile("res://data/blob.bin", out, error));
  CHECK_FALSE(error.empty());
  CHECK(out.empty());
}

TEST_CASE("file access: usable through IFileAccess", "[asset][file_access]") {
  auto memory = std::make_unique<MemoryFileAccess>();
  memory->addFile("res://data/blob.bin", bytesOf("polymorphic"));
  std::unique_ptr<IFileAccess> access = std::move(memory);

  std::vector<std::uint8_t> out;
  std::string error;
  REQUIRE(access->readFile("res://data/blob.bin", out, error));
  CHECK(error.empty());
  CHECK(out == bytesOf("polymorphic"));
}
