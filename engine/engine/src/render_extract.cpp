#include <yr/engine/render_extract.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <yr/asset/resource_types.h>
#include <yr/scene/light_node.h>
#include <yr/scene/mesh_instance_node.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

namespace yr::engine {

  namespace {

    [[nodiscard]] bool validTexture(const asset::Texture* texture) noexcept {
      if (texture == nullptr || !texture->uid().valid() || texture->state() != asset::LoadState::kReady) {
        return false;
      }
      const std::uint64_t pixels = static_cast<std::uint64_t>(texture->width) * texture->height;
      if (pixels > std::numeric_limits<std::uint64_t>::max() / 4) {
        return false;
      }
      const std::uint64_t expected = pixels * 4;
      return texture->rgba8.size() == expected;
    }

    [[nodiscard]] bool textureReady(const obj::Ref<asset::Texture>& texture) noexcept {
      return !texture.valid() || validTexture(texture.get());
    }

    [[nodiscard]] bool materialTexturesReady(const asset::Material& material) noexcept {
      return textureReady(material.baseColorTexture) && textureReady(material.normalTexture) &&
             textureReady(material.metallicRoughnessTexture) && textureReady(material.emissiveTexture);
    }

    void appendTexture(const asset::Texture& texture, render::RenderSnapshot& out) {
      render::TextureUpload upload;
      upload.texture.value = texture.uid().value;
      upload.width = texture.width;
      upload.height = texture.height;
      upload.colorSpace = texture.colorSpace == asset::TextureColorSpace::kLinear ? render::TextureColorSpace::kLinear
                                                                                  : render::TextureColorSpace::kSrgb;
      upload.rgba8 = texture.rgba8;
      out.uploads.textures.push_back(std::move(upload));
    }

    void appendReleases(const std::unordered_set<asset::AssetUID>& resident,
                        const std::unordered_set<asset::AssetUID>& current, std::vector<render::AssetHandle>& out) {
      std::vector<asset::AssetUID> removed;
      for (asset::AssetUID uid : resident) {
        if (!current.contains(uid)) {
          removed.push_back(uid);
        }
      }
      std::sort(removed.begin(), removed.end(),
                [](asset::AssetUID lhs, asset::AssetUID rhs) { return lhs.value < rhs.value; });
      out.reserve(removed.size());
      for (asset::AssetUID uid : removed) {
        out.push_back(render::AssetHandle{uid.value});
      }
    }

    [[nodiscard]] render::LightPod toLightPod(const scene::LightNode& light, const math::Mat4& world) {
      render::LightPod pod;
      pod.type = light.type() == scene::LightType::kPoint ? render::LightType::kPoint : render::LightType::kDirectional;
      pod.position[0] = world[12];
      pod.position[1] = world[13];
      pod.position[2] = world[14];
      const math::Vec3 direction = light.direction();
      pod.direction[0] = direction.x;
      pod.direction[1] = direction.y;
      pod.direction[2] = direction.z;
      const math::Vec3 color = light.color();
      pod.color[0] = color.x;
      pod.color[1] = color.y;
      pod.color[2] = color.z;
      pod.intensity = light.intensity();
      pod.range = light.range();
      return pod;
    }

    void collectPreorder(const scene::Node& node, render::RenderSnapshot& out,
                         const std::unordered_set<asset::AssetUID>& residentMeshes,
                         const std::unordered_set<asset::AssetUID>& residentMaterials,
                         const std::unordered_set<asset::AssetUID>& residentTextures,
                         std::unordered_set<asset::AssetUID>& currentMeshes,
                         std::unordered_set<asset::AssetUID>& currentMaterials,
                         std::unordered_set<asset::AssetUID>& currentTextures) {
      if (const auto* light = dynamic_cast<const scene::LightNode*>(&node); light != nullptr) {
        if (out.lightCount < render::RenderSnapshot::kMaxLights) {
          out.lights[out.lightCount++] = toLightPod(*light, node.globalTransform());
        }
      }

      if (const auto* instance = dynamic_cast<const scene::MeshInstanceNode*>(&node); instance != nullptr) {
        const asset::Mesh* const mesh = instance->mesh().get();
        const asset::Material* const material = instance->material().get();
        if (mesh != nullptr && material != nullptr && mesh->state() == asset::LoadState::kReady &&
            material->state() == asset::LoadState::kReady && materialTexturesReady(*material) && mesh->uid().valid() &&
            material->uid().valid()) {
          render::DrawItem item;
          item.object.value = node.id().raw();
          const math::Mat4 world = node.globalTransform();
          for (std::size_t index = 0; index < 16; ++index) {
            item.world.m[index] = world.m_[index];
          }
          item.mesh.value = mesh->uid().value;
          item.material.value = material->uid().value;
          out.items.push_back(item);

          if (currentMeshes.insert(mesh->uid()).second && !residentMeshes.contains(mesh->uid())) {
            render::MeshUpload upload;
            upload.mesh.value = mesh->uid().value;
            upload.vertices.reserve(mesh->vertices.size());
            for (const asset::MeshVertex& vertex : mesh->vertices) {
              render::MeshVertexPod pod;
              pod.position[0] = vertex.position.x;
              pod.position[1] = vertex.position.y;
              pod.position[2] = vertex.position.z;
              pod.normal[0] = vertex.normal.x;
              pod.normal[1] = vertex.normal.y;
              pod.normal[2] = vertex.normal.z;
              pod.uv[0] = vertex.u;
              pod.uv[1] = vertex.v;
              upload.vertices.push_back(pod);
            }
            upload.indices = mesh->indices;
            out.uploads.meshes.push_back(std::move(upload));
          }

          const std::array<const obj::Ref<asset::Texture>*, 4> textureSlots{
              &material->baseColorTexture,
              &material->normalTexture,
              &material->metallicRoughnessTexture,
              &material->emissiveTexture,
          };
          for (const auto* slot : textureSlots) {
            const asset::Texture* texture = slot->get();
            if (texture == nullptr) {
              continue;
            }
            const asset::AssetUID textureUid = texture->uid();
            if (!textureUid.valid() || texture->state() != asset::LoadState::kReady) {
              continue;
            }
            if (currentTextures.insert(textureUid).second && !residentTextures.contains(textureUid)) {
              appendTexture(*texture, out);
            }
          }

          if (currentMaterials.insert(material->uid()).second && !residentMaterials.contains(material->uid())) {
            render::MaterialUpload upload;
            upload.material.value = material->uid().value;
            upload.baseColorTexture.value =
                material->baseColorTexture.valid() ? material->baseColorTexture->uid().value : 0;
            upload.normalTexture.value = material->normalTexture.valid() ? material->normalTexture->uid().value : 0;
            upload.metallicRoughnessTexture.value =
                material->metallicRoughnessTexture.valid() ? material->metallicRoughnessTexture->uid().value : 0;
            upload.emissiveTexture.value =
                material->emissiveTexture.valid() ? material->emissiveTexture->uid().value : 0;
            upload.baseColorFactor[0] = material->baseColorFactor.x;
            upload.baseColorFactor[1] = material->baseColorFactor.y;
            upload.baseColorFactor[2] = material->baseColorFactor.z;
            upload.baseColorFactor[3] = material->baseColorAlpha;
            upload.metallic = material->metallic;
            upload.roughness = material->roughness;
            upload.normalScale = material->normalScale;
            upload.emissiveFactor[0] = material->emissive.x;
            upload.emissiveFactor[1] = material->emissive.y;
            upload.emissiveFactor[2] = material->emissive.z;
            upload.alphaCutoff = material->alphaCutoff;
            upload.alphaMode = material->alphaMode == asset::AlphaMode::kMask ? render::MaterialAlphaMode::kMask
                                                                              : render::MaterialAlphaMode::kOpaque;
            out.uploads.materials.push_back(upload);
          }
        }
      }

      const std::size_t childCount = node.childCount();
      for (std::size_t child = 0; child < childCount; ++child) {
        collectPreorder(*node.childAt(child), out, residentMeshes, residentMaterials, residentTextures, currentMeshes,
                        currentMaterials, currentTextures);
      }
    }

  } // namespace

  void RenderExtractSystem::extract(const scene::SceneTree& tree, const render::CameraPod& camera,
                                    render::RenderSnapshot& out) {
    out.camera = camera;
    out.lightCount = 0;
    out.lights = {};
    out.shadow = shadow_;
    out.items.clear();
    out.uploads.meshes.clear();
    out.uploads.textures.clear();
    out.uploads.materials.clear();
    out.releases.meshes.clear();
    out.releases.materials.clear();
    out.releases.textures.clear();

    std::unordered_set<asset::AssetUID> currentMeshes;
    std::unordered_set<asset::AssetUID> currentMaterials;
    std::unordered_set<asset::AssetUID> currentTextures;
    if (const scene::Node* const root = tree.root(); root != nullptr) {
      collectPreorder(*root, out, residentMeshes_, residentMaterials_, residentTextures_, currentMeshes,
                      currentMaterials, currentTextures);
    }

    appendReleases(residentMeshes_, currentMeshes, out.releases.meshes);
    appendReleases(residentMaterials_, currentMaterials, out.releases.materials);
    appendReleases(residentTextures_, currentTextures, out.releases.textures);
    residentMeshes_.swap(currentMeshes);
    residentMaterials_.swap(currentMaterials);
    residentTextures_.swap(currentTextures);
  }

  void RenderExtractSystem::reset() noexcept {
    residentMeshes_.clear();
    residentMaterials_.clear();
    residentTextures_.clear();
  }

} // namespace yr::engine
