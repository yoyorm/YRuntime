#pragma once

#include <utility>

#include <yr/asset/resource_types.h>
#include <yr/object/ref.h>
#include <yr/scene/node.h>

namespace yr::scene {

  class MeshInstanceNode final : public Node {
  public:
    MeshInstanceNode() = default;
    ~MeshInstanceNode() override = default;

    [[nodiscard]] const obj::Ref<asset::Mesh>& mesh() const noexcept { return mesh_; }
    [[nodiscard]] const obj::Ref<asset::Material>& material() const noexcept { return material_; }
    void setMesh(obj::Ref<asset::Mesh> mesh) noexcept { mesh_ = std::move(mesh); }
    void setMaterial(obj::Ref<asset::Material> material) noexcept { material_ = std::move(material); }

    [[nodiscard]] const obj::ClassInfo* classInfo() const noexcept override;

  private:
    obj::Ref<asset::Mesh> mesh_;
    obj::Ref<asset::Material> material_;
  };

  [[nodiscard]] const obj::ClassInfo& meshInstanceNodeClassInfo() noexcept;

} // namespace yr::scene
