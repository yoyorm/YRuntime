#include <yr/engine/render_extract.h>

#include <cstddef>

#include <yr/scene/node.h>
#include <yr/scene/scene_tree.h>

namespace yr::engine {

  namespace {

    void collectPreorder(const scene::Node& node, render::RenderSnapshot& out) {
      render::DrawItem item;
      item.object.value = node.id().raw();
      const math::Mat4 world = node.globalTransform();
      for (std::size_t index = 0; index < 16; ++index) {
        item.world.m[index] = world.m_[index];
      }
      out.items.push_back(item);

      const std::size_t childCount = node.childCount();
      for (std::size_t child = 0; child < childCount; ++child) {
        collectPreorder(*node.childAt(child), out);
      }
    }

  } // namespace

  void RenderExtractSystem::extract(const scene::SceneTree& tree, const render::CameraPod& camera,
                                    render::RenderSnapshot& out) const {
    out.camera = camera;
    out.items.clear();

    const scene::Node* const root = tree.root();
    if (root == nullptr) {
      return;
    }
    collectPreorder(*root, out);
  }

} // namespace yr::engine