#include "arcane_editor/scene_render.hpp"

#include <functional>

#include "arcane/builtin_components.hpp"
#include "arcane/node.hpp"

namespace arcane::editor {

namespace {

void visit_preorder(Node* node, const std::function<void(Node*)>& visit) {
  visit(node);
  for (const auto& child : node->children()) {
    visit_preorder(child.get(), visit);
  }
}

void visit_scene_preorder(Scene& scene, const std::function<void(Node*)>& visit) {
  for (const auto& root : scene.roots()) {
    visit_preorder(root.get(), visit);
  }
}

}  // namespace

void render_scene(Scene& scene, SpriteRenderer& renderer, TextureCache& textures, int width, int height) {
  float cam_x = 0.0f, cam_y = 0.0f, cam_zoom = 1.0f;
  visit_scene_preorder(scene, [&](Node* node) {
    auto* camera = static_cast<Camera*>(node->find_component("Camera"));
    if (camera && camera->primary) {
      const auto world = node->world_transform();
      cam_x = world.position.x;
      cam_y = world.position.y;
      cam_zoom = camera->zoom;
    }
  });

  renderer.set_camera(cam_x, cam_y, cam_zoom);
  renderer.begin_frame(width, height, Color{0.12f, 0.12f, 0.16f, 1.0f});

  visit_scene_preorder(scene, [&](Node* node) {
    auto* sprite = static_cast<Sprite*>(node->find_component("Sprite"));
    if (!sprite) return;
    Texture* texture = textures.get(sprite->texture);
    if (!texture) return;
    const auto world = node->world_transform();
    renderer.draw(SpriteDraw{
        .texture = texture,
        .x = world.position.x,
        .y = world.position.y,
        .width = sprite->width * world.scale.x,
        .height = sprite->height * world.scale.y,
        .rotation_radians = world.rotation_radians,
        .tint = sprite->tint,
    });
  });

  renderer.end_frame();
}

}  // namespace arcane::editor
