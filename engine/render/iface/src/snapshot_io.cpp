#include <yr/render/snapshot_io.h>

#include <bit>
#include <cstring>
#include <limits>
#include <utility>

namespace {

  constexpr std::uint8_t kMagic[6] = {'Y', 'R', 'S', 'N', 'A', 'P'};
  constexpr std::uint32_t kVersion = 2;
  constexpr std::uint64_t kItemBytes = 8 + (16 * 4) + 8 + 8;
  constexpr std::uint64_t kMeshVertexBytes = 8 * 4;

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
      if (count != 0) {
        std::memcpy(out, data_ + offset_, count);
      }
      offset_ += count;
      return true;
    }

    [[nodiscard]] bool readU32(std::uint32_t& out) noexcept {
      std::uint8_t bytes[4]{};
      if (!readBytes(bytes, sizeof(bytes))) {
        return false;
      }
      out = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
            (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
      return true;
    }

    [[nodiscard]] bool readU64(std::uint64_t& out) noexcept {
      std::uint8_t bytes[8]{};
      if (!readBytes(bytes, sizeof(bytes))) {
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

  [[nodiscard]] bool readCount(Reader& reader, std::uint64_t& count, std::uint64_t minBytesPerElement) {
    return reader.readU64(count) && count <= reader.remaining() / minBytesPerElement &&
           count <= std::numeric_limits<std::size_t>::max();
  }

  [[nodiscard]] bool readHandles(Reader& reader, std::vector<yr::render::AssetHandle>& handles) {
    std::uint64_t count = 0;
    if (!readCount(reader, count, 8)) {
      return false;
    }
    handles.resize(static_cast<std::size_t>(count));
    for (yr::render::AssetHandle& handle : handles) {
      if (!reader.readU64(handle.value)) {
        return false;
      }
    }
    return true;
  }

} // namespace

namespace yr::render {

  std::uint64_t snapshotByteSize(const RenderSnapshot& snapshot) noexcept {
    std::uint64_t size = 6 + 4 + 8 + 2 * 16 * 4 + 3 * 4 + 8;
    size += static_cast<std::uint64_t>(snapshot.items.size()) * kItemBytes;
    size += 8;
    for (const MeshUpload& mesh : snapshot.uploads.meshes) {
      size += 8 + 8 + static_cast<std::uint64_t>(mesh.vertices.size()) * kMeshVertexBytes + 8 +
              static_cast<std::uint64_t>(mesh.indices.size()) * 4;
    }
    size += 8;
    for (const TextureUpload& texture : snapshot.uploads.textures) {
      size += 8 + 4 + 4 + 8 + static_cast<std::uint64_t>(texture.rgba8.size());
    }
    size += 8 + static_cast<std::uint64_t>(snapshot.uploads.materials.size()) * (8 + 8 + 4 * 4 + 4 + 4);
    size += 8 + static_cast<std::uint64_t>(snapshot.releases.meshes.size()) * 8;
    size += 8 + static_cast<std::uint64_t>(snapshot.releases.materials.size()) * 8;
    size += 8 + static_cast<std::uint64_t>(snapshot.releases.textures.size()) * 8;
    return size;
  }

  std::vector<std::uint8_t> encodeSnapshot(const RenderSnapshot& snapshot) {
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(snapshotByteSize(snapshot)));

    out.insert(out.end(), kMagic, kMagic + sizeof(kMagic));
    writeU32(out, kVersion);
    writeU64(out, snapshot.frame_index);
    writeTransform(out, snapshot.camera.view);
    writeTransform(out, snapshot.camera.proj);
    for (float value : snapshot.camera.eye) {
      writeF32(out, value);
    }

    writeU64(out, snapshot.items.size());
    for (const DrawItem& item : snapshot.items) {
      writeU64(out, item.object.value);
      writeTransform(out, item.world);
      writeU64(out, item.mesh.value);
      writeU64(out, item.material.value);
    }

    writeU64(out, snapshot.uploads.meshes.size());
    for (const MeshUpload& mesh : snapshot.uploads.meshes) {
      writeU64(out, mesh.mesh.value);
      writeU64(out, mesh.vertices.size());
      for (const MeshVertexPod& vertex : mesh.vertices) {
        for (float value : vertex.position) {
          writeF32(out, value);
        }
        for (float value : vertex.normal) {
          writeF32(out, value);
        }
        for (float value : vertex.uv) {
          writeF32(out, value);
        }
      }
      writeU64(out, mesh.indices.size());
      for (std::uint32_t index : mesh.indices) {
        writeU32(out, index);
      }
    }

    writeU64(out, snapshot.uploads.textures.size());
    for (const TextureUpload& texture : snapshot.uploads.textures) {
      writeU64(out, texture.texture.value);
      writeU32(out, texture.width);
      writeU32(out, texture.height);
      writeU64(out, texture.rgba8.size());
      out.insert(out.end(), texture.rgba8.begin(), texture.rgba8.end());
    }

    writeU64(out, snapshot.uploads.materials.size());
    for (const MaterialUpload& material : snapshot.uploads.materials) {
      writeU64(out, material.material.value);
      writeU64(out, material.baseColorTexture.value);
      for (float value : material.baseColorFactor) {
        writeF32(out, value);
      }
      writeF32(out, material.metallic);
      writeF32(out, material.roughness);
    }

    const auto writeReleases = [&out](const std::vector<AssetHandle>& handles) {
      writeU64(out, handles.size());
      for (AssetHandle handle : handles) {
        writeU64(out, handle.value);
      }
    };
    writeReleases(snapshot.releases.meshes);
    writeReleases(snapshot.releases.materials);
    writeReleases(snapshot.releases.textures);
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
    for (float& value : snapshot.camera.eye) {
      if (!reader.readF32(value)) {
        return false;
      }
    }

    std::uint64_t itemCount = 0;
    if (!readCount(reader, itemCount, kItemBytes)) {
      return false;
    }
    snapshot.items.resize(static_cast<std::size_t>(itemCount));
    for (DrawItem& item : snapshot.items) {
      if (!reader.readU64(item.object.value) || !reader.readTransform(item.world) || !reader.readU64(item.mesh.value) ||
          !reader.readU64(item.material.value)) {
        return false;
      }
    }

    std::uint64_t meshCount = 0;
    if (!readCount(reader, meshCount, 8 + 8 + 8)) {
      return false;
    }
    snapshot.uploads.meshes.resize(static_cast<std::size_t>(meshCount));
    for (MeshUpload& mesh : snapshot.uploads.meshes) {
      std::uint64_t vertexCount = 0;
      if (!reader.readU64(mesh.mesh.value) || !readCount(reader, vertexCount, kMeshVertexBytes + 8)) {
        return false;
      }
      mesh.vertices.resize(static_cast<std::size_t>(vertexCount));
      for (MeshVertexPod& vertex : mesh.vertices) {
        for (float& value : vertex.position) {
          if (!reader.readF32(value)) {
            return false;
          }
        }
        for (float& value : vertex.normal) {
          if (!reader.readF32(value)) {
            return false;
          }
        }
        for (float& value : vertex.uv) {
          if (!reader.readF32(value)) {
            return false;
          }
        }
      }
      std::uint64_t indexCount = 0;
      if (!readCount(reader, indexCount, 4)) {
        return false;
      }
      mesh.indices.resize(static_cast<std::size_t>(indexCount));
      for (std::uint32_t& index : mesh.indices) {
        if (!reader.readU32(index)) {
          return false;
        }
      }
    }

    std::uint64_t textureCount = 0;
    if (!readCount(reader, textureCount, 8 + 4 + 4 + 8)) {
      return false;
    }
    snapshot.uploads.textures.resize(static_cast<std::size_t>(textureCount));
    for (TextureUpload& texture : snapshot.uploads.textures) {
      std::uint64_t byteCount = 0;
      if (!reader.readU64(texture.texture.value) || !reader.readU32(texture.width) || !reader.readU32(texture.height) ||
          !reader.readU64(byteCount)) {
        return false;
      }
      const std::uint64_t pixels = static_cast<std::uint64_t>(texture.width) * texture.height;
      if (pixels > std::numeric_limits<std::uint64_t>::max() / 4) {
        return false;
      }
      const std::uint64_t expectedBytes = pixels * 4;
      if (!((byteCount == 0 && texture.width == 0 && texture.height == 0) ||
            (byteCount == expectedBytes && byteCount != 0)) ||
          byteCount > reader.remaining() || byteCount > std::numeric_limits<std::size_t>::max()) {
        return false;
      }
      texture.rgba8.resize(static_cast<std::size_t>(byteCount));
      if (!reader.readBytes(texture.rgba8.data(), texture.rgba8.size())) {
        return false;
      }
    }

    std::uint64_t materialCount = 0;
    constexpr std::uint64_t kMaterialBytes = 8 + 8 + 4 * 4 + 4 + 4;
    if (!readCount(reader, materialCount, kMaterialBytes)) {
      return false;
    }
    snapshot.uploads.materials.resize(static_cast<std::size_t>(materialCount));
    for (MaterialUpload& material : snapshot.uploads.materials) {
      if (!reader.readU64(material.material.value) || !reader.readU64(material.baseColorTexture.value)) {
        return false;
      }
      for (float& value : material.baseColorFactor) {
        if (!reader.readF32(value)) {
          return false;
        }
      }
      if (!reader.readF32(material.metallic) || !reader.readF32(material.roughness)) {
        return false;
      }
    }

    if (!readHandles(reader, snapshot.releases.meshes) || !readHandles(reader, snapshot.releases.materials) ||
        !readHandles(reader, snapshot.releases.textures) || reader.remaining() != 0) {
      return false;
    }

    out = std::move(snapshot);
    return true;
  }

} // namespace yr::render
