#include <yr/asset/gltf_loader.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <stb_image.h>
#include <tiny_gltf_v3.h>

#include <yr/asset/asset_uid.h>

namespace yr::asset {

  namespace {

    std::string_view viewOf(const tg3_str& text) noexcept {
      return text.data != nullptr ? std::string_view(text.data, text.len) : std::string_view{};
    }

    std::string stringOf(const tg3_str& text) {
      return std::string(viewOf(text));
    }

    struct AccessorView {
      const std::uint8_t* data = nullptr;
      std::uint64_t count = 0;
      std::uint32_t stride = 0;
      std::uint32_t componentSize = 0;
      std::uint32_t componentCount = 0;
      std::int32_t componentType = 0;

      [[nodiscard]] bool valid() const noexcept { return data != nullptr; }
    };

    [[nodiscard]] std::uint32_t componentSizeOf(std::int32_t componentType) noexcept {
      switch (componentType) {
      case TG3_COMPONENT_TYPE_BYTE:
      case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
        return 1;
      case TG3_COMPONENT_TYPE_SHORT:
      case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
        return 2;
      case TG3_COMPONENT_TYPE_INT:
      case TG3_COMPONENT_TYPE_UNSIGNED_INT:
      case TG3_COMPONENT_TYPE_FLOAT:
        return 4;
      case TG3_COMPONENT_TYPE_DOUBLE:
        return 8;
      default:
        return 0;
      }
    }

    [[nodiscard]] std::uint32_t componentCountOf(std::int32_t type) noexcept {
      switch (type) {
      case TG3_TYPE_SCALAR:
        return 1;
      case TG3_TYPE_VEC2:
        return 2;
      case TG3_TYPE_VEC3:
        return 3;
      case TG3_TYPE_VEC4:
        return 4;
      default:
        return 0;
      }
    }

    [[nodiscard]] bool resolveAccessor(const tg3_model& model, std::int32_t accessorIndex, AccessorView& out,
                                       std::string& error) {
      if (accessorIndex < 0 || static_cast<std::uint32_t>(accessorIndex) >= model.accessors_count) {
        error = "accessor 索引越界";
        return false;
      }
      const tg3_accessor& accessor = model.accessors[accessorIndex];
      if (accessor.buffer_view < 0 || static_cast<std::uint32_t>(accessor.buffer_view) >= model.buffer_views_count) {
        error = "accessor.bufferView 越界";
        return false;
      }

      const tg3_buffer_view& view = model.buffer_views[accessor.buffer_view];
      if (view.buffer < 0 || static_cast<std::uint32_t>(view.buffer) >= model.buffers_count) {
        error = "bufferView.buffer 越界";
        return false;
      }
      const tg3_buffer& buffer = model.buffers[view.buffer];
      if (buffer.data.data == nullptr) {
        error = "buffer 数据为空";
        return false;
      }

      const std::uint32_t componentSize = componentSizeOf(accessor.component_type);
      const std::uint32_t componentCount = componentCountOf(accessor.type);
      if (componentSize == 0 || componentCount == 0) {
        error = "不支持的 accessor componentType/type";
        return false;
      }
      const std::uint64_t elementSize = static_cast<std::uint64_t>(componentSize) * componentCount;
      if (accessor.byte_offset > std::numeric_limits<std::uint64_t>::max() - view.byte_offset) {
        error = "accessor.byteOffset 溢出";
        return false;
      }
      const std::uint64_t start = view.byte_offset + accessor.byte_offset;
      if (start > buffer.data.count) {
        error = "accessor 起始位置越界";
        return false;
      }

      const std::uint32_t stride = view.byte_stride != 0 ? view.byte_stride : static_cast<std::uint32_t>(elementSize);
      if (stride < elementSize) {
        error = "bufferView.byteStride 小于元素大小";
        return false;
      }

      if (accessor.count > 0) {
        if (elementSize > buffer.data.count - start) {
          error = "accessor 元素越界";
          return false;
        }
        const std::uint64_t available = buffer.data.count - start - elementSize;
        if ((accessor.count - 1) > available / stride) {
          error = "accessor 数据越界";
          return false;
        }
      }

      out.data = buffer.data.data + start;
      out.count = accessor.count;
      out.stride = stride;
      out.componentSize = componentSize;
      out.componentCount = componentCount;
      out.componentType = accessor.component_type;
      return true;
    }

    [[nodiscard]] float readFloat(const AccessorView& view, std::uint64_t index, std::uint32_t component) {
      float value = 0.0F;
      const std::uint8_t* const source =
          view.data + index * view.stride + static_cast<std::uint64_t>(component) * view.componentSize;
      std::memcpy(&value, source, sizeof(value));
      return value;
    }

    [[nodiscard]] std::uint32_t readIndex(const AccessorView& view, std::uint64_t index) {
      const std::uint8_t* const source = view.data + index * view.stride;
      std::uint32_t value = 0;
      for (std::uint32_t byte = 0; byte < view.componentSize; ++byte) {
        value |= static_cast<std::uint32_t>(source[byte]) << (byte * 8U);
      }
      return value;
    }

    [[nodiscard]] std::int32_t findAttribute(const tg3_primitive& primitive, std::string_view name) {
      for (std::uint32_t index = 0; index < primitive.attributes_count; ++index) {
        const tg3_str_int_pair& attribute = primitive.attributes[index];
        if (viewOf(attribute.key) == name) {
          return attribute.value;
        }
      }
      return -1;
    }

    [[nodiscard]] std::string formatErrors(const tinygltf3::ErrorStack& errors) {
      std::string message;
      for (std::uint32_t index = 0; index < errors.count(); ++index) {
        const tg3_error_entry* const entry = errors.entry(index);
        if (entry == nullptr) {
          continue;
        }
        if (!message.empty()) {
          message += "\n";
        }
        message += "glTF 错误";
        if (entry->json_path != nullptr) {
          message += " [";
          message += entry->json_path;
          message += "]";
        }
        if (entry->message != nullptr) {
          message += ": ";
          message += entry->message;
        }
      }
      if (message.empty()) {
        message = "glTF 解析失败";
      }
      return message;
    }

    // res://dir/file.gltf + "tex.png" -> res://dir/tex.png
    [[nodiscard]] bool resolveImagePath(std::string_view gltfResPath, std::string_view uri, std::string& out,
                                        std::string& error) {
      if (uri.starts_with("res://")) {
        const AssetPath canonical = canonicalizeAssetPath(uri);
        if (!canonical.ok()) {
          error = canonical.error;
          return false;
        }
        out = canonical.canonical;
        return true;
      }
      if (uri.starts_with("data:")) {
        error = "data URI 图片暂不支持";
        return false;
      }

      std::string base;
      const std::size_t slash = gltfResPath.rfind('/');
      if (slash == std::string_view::npos || slash < 6) {
        base = "res://";
      } else {
        base = std::string(gltfResPath.substr(0, slash));
      }
      const std::string combined = base.ends_with('/') ? base + std::string(uri) : base + "/" + std::string(uri);
      const AssetPath canonical = canonicalizeAssetPath(combined);
      if (!canonical.ok()) {
        error = canonical.error;
        return false;
      }
      out = canonical.canonical;
      return true;
    }

    struct LoadContext {
      LoadContext(const tg3_model& model_, std::string resPath_, std::filesystem::path assetRoot_,
                  std::vector<std::string>& warnings_)
          : model(model_), resPath(std::move(resPath_)), assetRoot(std::move(assetRoot_)), warnings(warnings_) {}

      const tg3_model& model;
      std::string resPath;
      std::filesystem::path assetRoot;
      std::vector<std::string>& warnings;
      std::unordered_map<std::int32_t, obj::Ref<Material>> materialCache;
      std::unordered_map<std::int32_t, obj::Ref<Texture>> textureCache;
      std::unordered_map<std::uint64_t, obj::Ref<Mesh>> meshCache;
      obj::Ref<Material> defaultMaterial;
    };

    void addWarning(LoadContext& context, std::string message) {
      context.warnings.push_back(std::move(message));
    }

    [[nodiscard]] obj::Ref<Texture> loadTexture(LoadContext& context, std::int32_t textureIndex) {
      if (textureIndex < 0 || static_cast<std::uint32_t>(textureIndex) >= context.model.textures_count) {
        return {};
      }
      const tg3_texture& texture = context.model.textures[textureIndex];
      const std::int32_t imageIndex = texture.source;
      if (imageIndex < 0 || static_cast<std::uint32_t>(imageIndex) >= context.model.images_count) {
        addWarning(context, "texture " + std::to_string(textureIndex) + ": source image 索引无效");
        return {};
      }

      const auto cached = context.textureCache.find(imageIndex);
      if (cached != context.textureCache.end()) {
        return cached->second;
      }

      const auto fail = [&](std::string message) -> obj::Ref<Texture> {
        addWarning(context, "image " + std::to_string(imageIndex) + ": " + std::move(message));
        context.textureCache.emplace(imageIndex, obj::Ref<Texture>{});
        return {};
      };

      const tg3_image& image = context.model.images[imageIndex];
      if (image.buffer_view >= 0) {
        return fail("GLB 内嵌图（bufferView）暂不支持");
      }
      const std::string_view uri = viewOf(image.uri);
      if (uri.empty()) {
        return fail("image 没有外部 URI");
      }

      std::string imageResPath;
      std::string pathError;
      if (!resolveImagePath(context.resPath, uri, imageResPath, pathError)) {
        return fail("无法解析图片路径：" + pathError);
      }
      const std::filesystem::path filePath =
          context.assetRoot / std::string(imageResPath.substr(std::string("res://").size()));

      obj::Ref<Texture> resource = obj::make_ref<Texture>();
      (void)resource->markLoading();
      resource->setUid(AssetUID::fromPath(imageResPath));
      resource->setSourcePath(imageResPath);

      int width = 0;
      int height = 0;
      int channels = 0;
      stbi_uc* const pixels = stbi_load(filePath.string().c_str(), &width, &height, &channels, 4);
      if (pixels == nullptr) {
        const char* const reason = stbi_failure_reason();
        const std::string text = reason != nullptr ? reason : "stbi_load 失败";
        (void)resource->markFailed(text);
        return fail("解码失败：" + text);
      }
      if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        (void)resource->markFailed("图片尺寸无效");
        return fail("图片尺寸无效");
      }

      const std::uint64_t pixelBytes = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
      if (pixelBytes > std::numeric_limits<std::size_t>::max()) {
        stbi_image_free(pixels);
        (void)resource->markFailed("图片过大");
        return fail("图片过大");
      }

      resource->width = static_cast<std::uint32_t>(width);
      resource->height = static_cast<std::uint32_t>(height);
      resource->rgba8.assign(pixels, pixels + static_cast<std::size_t>(pixelBytes));
      stbi_image_free(pixels);

      if (resource->rgba8.size() != static_cast<std::size_t>(pixelBytes)) {
        (void)resource->markFailed("RGBA 数据大小不匹配");
        return fail("RGBA 数据大小不匹配");
      }
      (void)resource->markReady();
      context.textureCache.emplace(imageIndex, resource);
      return resource;
    }

    [[nodiscard]] obj::Ref<Material> buildMaterial(LoadContext& context, std::int32_t materialIndex) {
      if (materialIndex < 0 || static_cast<std::uint32_t>(materialIndex) >= context.model.materials_count) {
        addWarning(context, "material 索引无效：" + std::to_string(materialIndex));
        return {};
      }
      const auto cached = context.materialCache.find(materialIndex);
      if (cached != context.materialCache.end()) {
        return cached->second;
      }

      const tg3_material& source = context.model.materials[materialIndex];
      obj::Ref<Material> material = obj::make_ref<Material>();
      (void)material->markLoading();
      material->setUid(AssetUID::fromPath(context.resPath + "#material/" + std::to_string(materialIndex)));
      material->setSourcePath(context.resPath);

      // glTF 默认值；再覆盖文件里的 factors。
      material->baseColorFactor = math::Vec3{1.0F, 1.0F, 1.0F};
      material->baseColorAlpha = 1.0F;
      material->metallic = 1.0F;
      material->roughness = 1.0F;
      material->emissive = math::Vec3{0.0F, 0.0F, 0.0F};
      material->doubleSided = false;
      material->alphaMode = AlphaMode::kOpaque;
      material->alphaCutoff = 0.5F;

      const tg3_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
      material->baseColorFactor =
          math::Vec3{static_cast<float>(pbr.base_color_factor[0]), static_cast<float>(pbr.base_color_factor[1]),
                     static_cast<float>(pbr.base_color_factor[2])};
      material->baseColorAlpha = static_cast<float>(pbr.base_color_factor[3]);
      material->metallic = static_cast<float>(pbr.metallic_factor);
      material->roughness = static_cast<float>(pbr.roughness_factor);
      material->emissive =
          math::Vec3{static_cast<float>(source.emissive_factor[0]), static_cast<float>(source.emissive_factor[1]),
                     static_cast<float>(source.emissive_factor[2])};
      material->doubleSided = source.double_sided != 0;
      material->alphaCutoff = static_cast<float>(source.alpha_cutoff);

      const std::string alphaMode = stringOf(source.alpha_mode);
      if (alphaMode.empty() || alphaMode == "OPAQUE") {
        material->alphaMode = AlphaMode::kOpaque;
      } else if (alphaMode == "MASK") {
        material->alphaMode = AlphaMode::kMask;
      } else if (alphaMode == "BLEND") {
        material->alphaMode = AlphaMode::kBlend;
      } else {
        material->alphaMode = AlphaMode::kOpaque;
        addWarning(context,
                   "material " + std::to_string(materialIndex) + ": 未知 alphaMode '" + alphaMode + "'，按 OPAQUE");
      }

      material->baseColorTexture = loadTexture(context, pbr.base_color_texture.index);
      material->normalTexture = loadTexture(context, source.normal_texture.index);
      material->metallicRoughnessTexture = loadTexture(context, pbr.metallic_roughness_texture.index);
      material->emissiveTexture = loadTexture(context, source.emissive_texture.index);

      (void)material->markReady();
      context.materialCache.emplace(materialIndex, material);
      return material;
    }

    [[nodiscard]] obj::Ref<Material> buildDefaultMaterial(LoadContext& context) {
      obj::Ref<Material> material = obj::make_ref<Material>();
      (void)material->markLoading();
      material->setUid(AssetUID::fromPath(context.resPath + "#material/default"));
      material->setSourcePath(context.resPath);
      material->baseColorFactor = math::Vec3{1.0F, 1.0F, 1.0F};
      material->baseColorAlpha = 1.0F;
      material->metallic = 1.0F;
      material->roughness = 1.0F;
      material->emissive = math::Vec3{0.0F, 0.0F, 0.0F};
      material->doubleSided = false;
      material->alphaMode = AlphaMode::kOpaque;
      material->alphaCutoff = 0.5F;
      (void)material->markReady();
      return material;
    }

    [[nodiscard]] obj::Ref<Mesh> buildMesh(LoadContext& context, std::int32_t meshIndex, std::uint32_t primitiveIndex) {
      const std::uint64_t cacheKey = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(meshIndex)) << 32U) |
                                     static_cast<std::uint32_t>(primitiveIndex);
      const auto cached = context.meshCache.find(cacheKey);
      if (cached != context.meshCache.end()) {
        return cached->second;
      }

      const auto fail = [&](std::string message) -> obj::Ref<Mesh> {
        addWarning(context, "mesh " + std::to_string(meshIndex) + " primitive " + std::to_string(primitiveIndex) +
                                ": " + std::move(message));
        context.meshCache.emplace(cacheKey, obj::Ref<Mesh>{});
        return {};
      };

      if (meshIndex < 0 || static_cast<std::uint32_t>(meshIndex) >= context.model.meshes_count) {
        return fail("mesh 索引越界");
      }
      const tg3_mesh& mesh = context.model.meshes[meshIndex];
      if (primitiveIndex >= mesh.primitives_count) {
        return fail("primitive 索引越界");
      }
      const tg3_primitive& primitive = mesh.primitives[primitiveIndex];
      if (primitive.mode != -1 && primitive.mode != TG3_MODE_TRIANGLES) {
        return fail("不支持的 primitive.mode " + std::to_string(primitive.mode));
      }

      const std::int32_t positionIndex = findAttribute(primitive, "POSITION");
      if (positionIndex < 0) {
        return fail("缺少 POSITION");
      }

      AccessorView positions;
      std::string accessorError;
      if (!resolveAccessor(context.model, positionIndex, positions, accessorError)) {
        return fail("POSITION: " + accessorError);
      }
      if (positions.componentType != TG3_COMPONENT_TYPE_FLOAT || positions.componentCount != 3) {
        return fail("POSITION 必须是 FLOAT VEC3");
      }
      if (positions.count == 0) {
        return fail("POSITION 为空");
      }
      if (positions.count > std::numeric_limits<std::uint32_t>::max()) {
        return fail("POSITION 顶点数超过 u32");
      }

      AccessorView normals;
      if (const std::int32_t normalIndex = findAttribute(primitive, "NORMAL"); normalIndex >= 0) {
        if (!resolveAccessor(context.model, normalIndex, normals, accessorError)) {
          return fail("NORMAL: " + accessorError);
        }
        if (normals.componentType != TG3_COMPONENT_TYPE_FLOAT || normals.componentCount != 3 ||
            normals.count != positions.count) {
          return fail("NORMAL 必须是同顶点数的 FLOAT VEC3");
        }
      }

      AccessorView uvs;
      if (const std::int32_t uvIndex = findAttribute(primitive, "TEXCOORD_0"); uvIndex >= 0) {
        if (!resolveAccessor(context.model, uvIndex, uvs, accessorError)) {
          return fail("TEXCOORD_0: " + accessorError);
        }
        if (uvs.componentType != TG3_COMPONENT_TYPE_FLOAT || uvs.componentCount != 2 || uvs.count != positions.count) {
          return fail("TEXCOORD_0 必须是同顶点数的 FLOAT VEC2");
        }
      }

      AccessorView indexView;
      if (primitive.indices >= 0) {
        if (!resolveAccessor(context.model, primitive.indices, indexView, accessorError)) {
          return fail("indices: " + accessorError);
        }
        if (indexView.componentCount != 1 || (indexView.componentType != TG3_COMPONENT_TYPE_UNSIGNED_BYTE &&
                                              indexView.componentType != TG3_COMPONENT_TYPE_UNSIGNED_SHORT &&
                                              indexView.componentType != TG3_COMPONENT_TYPE_UNSIGNED_INT)) {
          return fail("indices 必须是 u8/u16/u32 SCALAR");
        }
      }

      obj::Ref<Mesh> resource = obj::make_ref<Mesh>();
      (void)resource->markLoading();
      resource->setUid(AssetUID::fromPath(context.resPath + "#mesh/" + std::to_string(meshIndex) + "/primitive/" +
                                          std::to_string(primitiveIndex)));
      resource->setSourcePath(context.resPath);

      resource->vertices.resize(static_cast<std::size_t>(positions.count));
      for (std::size_t index = 0; index < resource->vertices.size(); ++index) {
        MeshVertex vertex;
        vertex.position =
            math::Vec3{readFloat(positions, index, 0), readFloat(positions, index, 1), readFloat(positions, index, 2)};
        if (normals.valid()) {
          vertex.normal =
              math::Vec3{readFloat(normals, index, 0), readFloat(normals, index, 1), readFloat(normals, index, 2)};
        }
        if (uvs.valid()) {
          vertex.u = readFloat(uvs, index, 0);
          vertex.v = readFloat(uvs, index, 1);
        }
        resource->vertices[index] = vertex;
      }

      if (indexView.valid()) {
        resource->indices.reserve(static_cast<std::size_t>(indexView.count));
        for (std::size_t index = 0; index < static_cast<std::size_t>(indexView.count); ++index) {
          resource->indices.push_back(readIndex(indexView, index));
        }
      } else {
        resource->indices.reserve(resource->vertices.size());
        for (std::size_t index = 0; index < resource->vertices.size(); ++index) {
          resource->indices.push_back(static_cast<std::uint32_t>(index));
        }
      }

      (void)resource->markReady();
      context.meshCache.emplace(cacheKey, resource);
      return resource;
    }

    [[nodiscard]] math::Transform localTransformOf(LoadContext& context, const tg3_node& node) {
      math::Transform result;
      if (node.has_matrix == 0) {
        result.position = math::Vec3{static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]),
                                     static_cast<float>(node.translation[2])};
        result.rotation = math::Quat{static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]),
                                     static_cast<float>(node.rotation[2]), static_cast<float>(node.rotation[3])}
                              .normalized();
        result.scale = math::Vec3{static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]),
                                  static_cast<float>(node.scale[2])};
        return result;
      }

      const auto warnMatrix = [&](std::string message) {
        addWarning(context, "node '" + stringOf(node.name) + "': matrix " + std::move(message));
      };
      result.position = math::Vec3{static_cast<float>(node.matrix[12]), static_cast<float>(node.matrix[13]),
                                   static_cast<float>(node.matrix[14])};

      math::Vec3 columns[3] = {
          math::Vec3{static_cast<float>(node.matrix[0]), static_cast<float>(node.matrix[1]),
                     static_cast<float>(node.matrix[2])},
          math::Vec3{static_cast<float>(node.matrix[4]), static_cast<float>(node.matrix[5]),
                     static_cast<float>(node.matrix[6])},
          math::Vec3{static_cast<float>(node.matrix[8]), static_cast<float>(node.matrix[9]),
                     static_cast<float>(node.matrix[10])},
      };
      result.scale = math::Vec3{columns[0].length(), columns[1].length(), columns[2].length()};
      if (result.scale.x <= 1.0e-8F || result.scale.y <= 1.0e-8F || result.scale.z <= 1.0e-8F) {
        warnMatrix("含近零缩放列；保留平移 / 缩放并使用 identity rotation");
        return result;
      }

      const math::Vec3 normalizedColumns[3] = {columns[0] / result.scale.x, columns[1] / result.scale.y,
                                               columns[2] / result.scale.z};
      const float determinant =
          math::dot(normalizedColumns[0], math::cross(normalizedColumns[1], normalizedColumns[2]));
      if (determinant < 0.0F) {
        result.scale.x = -result.scale.x;
      }

      const math::Vec3 rotationColumns[3] = {columns[0] / result.scale.x, columns[1] / result.scale.y,
                                             columns[2] / result.scale.z};
      constexpr float kDecompositionTolerance = 1.0e-4F;
      const bool hasShear = std::abs(math::dot(rotationColumns[0], rotationColumns[1])) > kDecompositionTolerance ||
                            std::abs(math::dot(rotationColumns[0], rotationColumns[2])) > kDecompositionTolerance ||
                            std::abs(math::dot(rotationColumns[1], rotationColumns[2])) > kDecompositionTolerance;
      const bool isAffine = std::abs(static_cast<float>(node.matrix[3])) <= kDecompositionTolerance &&
                            std::abs(static_cast<float>(node.matrix[7])) <= kDecompositionTolerance &&
                            std::abs(static_cast<float>(node.matrix[11])) <= kDecompositionTolerance &&
                            std::abs(static_cast<float>(node.matrix[15]) - 1.0F) <= kDecompositionTolerance;
      if (hasShear || !isAffine) {
        warnMatrix("含 shear 或非仿射分量；保留平移 / 缩放并使用 identity rotation");
        return result;
      }

      math::Mat4 rotationMatrix;
      rotationMatrix[0] = rotationColumns[0].x;
      rotationMatrix[1] = rotationColumns[0].y;
      rotationMatrix[2] = rotationColumns[0].z;
      rotationMatrix[4] = rotationColumns[1].x;
      rotationMatrix[5] = rotationColumns[1].y;
      rotationMatrix[6] = rotationColumns[1].z;
      rotationMatrix[8] = rotationColumns[2].x;
      rotationMatrix[9] = rotationColumns[2].y;
      rotationMatrix[10] = rotationColumns[2].z;
      result.rotation = math::Quat::fromRotationMatrix(rotationMatrix);
      return result;
    }

    [[nodiscard]] bool buildNode(LoadContext& context, std::int32_t nodeIndex, std::vector<std::uint8_t>& active,
                                 GltfNode& out) {
      if (nodeIndex < 0 || static_cast<std::uint32_t>(nodeIndex) >= context.model.nodes_count) {
        addWarning(context, "node 索引越界：" + std::to_string(nodeIndex));
        return false;
      }
      if (active[nodeIndex] != 0) {
        addWarning(context, "node 环或重复父路径：" + std::to_string(nodeIndex));
        return false;
      }
      active[nodeIndex] = 1;

      const tg3_node& source = context.model.nodes[nodeIndex];
      GltfNode node;
      node.name = stringOf(source.name);
      node.localTransform = localTransformOf(context, source);

      if (source.mesh >= 0) {
        if (static_cast<std::uint32_t>(source.mesh) >= context.model.meshes_count) {
          addWarning(context, "node " + std::to_string(nodeIndex) + ": mesh 索引越界");
        } else {
          const tg3_mesh& mesh = context.model.meshes[source.mesh];
          for (std::uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex) {
            obj::Ref<Mesh> meshResource = buildMesh(context, source.mesh, primitiveIndex);
            if (!meshResource) {
              continue;
            }
            const tg3_primitive& primitive = mesh.primitives[primitiveIndex];
            obj::Ref<Material> material;
            if (primitive.material >= 0) {
              material = buildMaterial(context, primitive.material);
            }
            if (!material) {
              material = context.defaultMaterial;
            }
            node.primitives.push_back(GltfPrimitive{std::move(meshResource), std::move(material)});
          }
        }
      }

      for (std::uint32_t childIndex = 0; childIndex < source.children_count; ++childIndex) {
        GltfNode child;
        if (buildNode(context, source.children[childIndex], active, child)) {
          node.children.push_back(std::move(child));
        }
      }

      active[nodeIndex] = 0;
      out = std::move(node);
      return true;
    }

  } // namespace

  GltfLoadResult loadGltfScene(std::string_view resPath, std::string_view assetRoot) {
    GltfLoadResult result;

    const AssetPath canonical = canonicalizeAssetPath(resPath);
    if (!canonical.ok()) {
      result.error = "非法 scene 路径：" + canonical.error;
      return result;
    }
    if (assetRoot.empty()) {
      result.error = "assetRoot 不能为空";
      return result;
    }

    const std::string resPathString = canonical.canonical;
    const std::filesystem::path filePath =
        std::filesystem::path(std::string(assetRoot)) / resPathString.substr(std::string("res://").size());

    tinygltf3::Model model;
    tinygltf3::ErrorStack errors;
    const tg3_error_code code = tinygltf3::parse_file(model, errors, filePath.string().c_str());
    if (code != TG3_OK) {
      result.error = formatErrors(errors);
      return result;
    }

    const tg3_model& source = *model.get();
    for (std::uint32_t index = 0; index < source.extensions_required_count; ++index) {
      const std::string name = stringOf(source.extensions_required[index]);
      result.error = "required extension 不受支持：" + name;
      return result;
    }
    for (std::uint32_t index = 0; index < source.extensions_used_count; ++index) {
      result.warnings.push_back("unsupported extension: " + stringOf(source.extensions_used[index]) + " (ignored)");
    }

    if (source.scenes_count == 0) {
      result.error = "glTF 没有 scene";
      return result;
    }
    std::int32_t sceneIndex = source.default_scene;
    if (sceneIndex < 0 || static_cast<std::uint32_t>(sceneIndex) >= source.scenes_count) {
      sceneIndex = 0;
    }
    const tg3_scene& scene = source.scenes[sceneIndex];

    LoadContext context{source, resPathString, std::filesystem::path(std::string(assetRoot)), result.warnings};
    context.defaultMaterial = buildDefaultMaterial(context);

    result.scene.name = stringOf(scene.name);
    if (result.scene.name.empty()) {
      result.scene.name = filePath.stem().string();
    }

    std::vector<std::uint8_t> active(source.nodes_count, 0);
    for (std::uint32_t index = 0; index < scene.nodes_count; ++index) {
      GltfNode root;
      if (buildNode(context, scene.nodes[index], active, root)) {
        result.scene.roots.push_back(std::move(root));
      }
    }

    result.ok = true;
    return result;
  }

} // namespace yr::asset
