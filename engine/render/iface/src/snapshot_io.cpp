#include <yr/render/snapshot_io.h>

#include <bit>
#include <cstring>

namespace {

  constexpr std::uint8_t kMagic[6] = {'Y', 'R', 'S', 'N', 'A', 'P'};
  constexpr std::uint32_t kVersion = 1;
  // 单个 DrawItem 的编码字节数：object u64 + world 16×f32 + mesh u64 + material u64。
  constexpr std::size_t kItemBytes = 8 + (16 * 4) + 8 + 8;

  void writeU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
  }

  void writeU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
      out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
  }

  // 固定按小端写 f32 的位模式，不依赖宿主端序。
  void writeF32(std::vector<std::uint8_t>& out, float value) {
    writeU32(out, std::bit_cast<std::uint32_t>(value));
  }

  void writeTransform(std::vector<std::uint8_t>& out, const yr::render::TransformPod& transform) {
    for (float value : transform.m) {
      writeF32(out, value);
    }
  }

  class Reader {
  public:
    Reader(const std::uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

    [[nodiscard]] bool readBytes(std::uint8_t* out, std::size_t count) noexcept {
      if (remaining() < count) {
        return false;
      }
      std::memcpy(out, data_ + offset_, count);
      offset_ += count;
      return true;
    }

    [[nodiscard]] bool readU32(std::uint32_t& out) noexcept {
      std::uint8_t bytes[4]{};
      if (!readBytes(bytes, 4)) {
        return false;
      }
      out = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
            (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
      return true;
    }

    [[nodiscard]] bool readU64(std::uint64_t& out) noexcept {
      std::uint8_t bytes[8]{};
      if (!readBytes(bytes, 8)) {
        return false;
      }
      out = 0;
      for (int index = 0; index < 8; ++index) {
        out |= static_cast<std::uint64_t>(bytes[index]) << (index * 8);
      }
      return true;
    }

    [[nodiscard]] bool readF32(float& out) noexcept {
      std::uint32_t bits = 0;
      if (!readU32(bits)) {
        return false;
      }
      out = std::bit_cast<float>(bits);
      return true;
    }

    [[nodiscard]] bool readTransform(yr::render::TransformPod& out) noexcept {
      for (float& value : out.m) {
        if (!readF32(value)) {
          return false;
        }
      }
      return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept { return size_ - offset_; }

  private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t offset_ = 0;
  };

} // namespace

namespace yr::render {

  std::vector<std::uint8_t> encodeSnapshot(const RenderSnapshot& snapshot) {
    std::vector<std::uint8_t> out;
    out.reserve(6 + 4 + 8 + (16 * 4 * 2) + 8 + (snapshot.items.size() * kItemBytes));

    out.insert(out.end(), kMagic, kMagic + sizeof(kMagic));
    writeU32(out, kVersion);
    writeU64(out, snapshot.frame_index);
    writeTransform(out, snapshot.camera.view);
    writeTransform(out, snapshot.camera.proj);

    writeU64(out, snapshot.items.size());
    for (const DrawItem& item : snapshot.items) {
      writeU64(out, item.object.value);
      writeTransform(out, item.world);
      writeU64(out, item.mesh.value);
      writeU64(out, item.material.value);
    }
    return out;
  }

  bool decodeSnapshot(const std::uint8_t* data, std::size_t size, RenderSnapshot& out) {
    if (data == nullptr) {
      return false;
    }

    Reader reader(data, size);
    std::uint8_t magic[6]{};
    if (!reader.readBytes(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(magic)) != 0) {
      return false;
    }

    std::uint32_t version = 0;
    if (!reader.readU32(version) || version != kVersion) {
      return false;
    }

    RenderSnapshot snapshot;
    if (!reader.readU64(snapshot.frame_index) || !reader.readTransform(snapshot.camera.view) ||
        !reader.readTransform(snapshot.camera.proj)) {
      return false;
    }

    std::uint64_t itemCount = 0;
    if (!reader.readU64(itemCount) || itemCount > reader.remaining() / kItemBytes) {
      return false;
    }

    snapshot.items.resize(static_cast<std::size_t>(itemCount));
    for (DrawItem& item : snapshot.items) {
      if (!reader.readU64(item.object.value) || !reader.readTransform(item.world) || !reader.readU64(item.mesh.value) ||
          !reader.readU64(item.material.value)) {
        return false;
      }
    }

    if (reader.remaining() != 0) {
      return false;
    }

    out = std::move(snapshot);
    return true;
  }

} // namespace yr::render