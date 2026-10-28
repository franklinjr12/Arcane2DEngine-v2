#include "arcane_editor/scene_render.hpp"

#include <algorithm>
#include <cmath>
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

void draw_tilemap(const Tilemap& map, const WorldTransform2D& world, const Tileset& tileset, Texture& texture,
                  SpriteRenderer& renderer, float cam_x, float cam_y, float view_w, float view_h) {
  const float cell_w = map.tile_width * world.scale.x;
  const float cell_h = map.tile_height * world.scale.y;
  if (cell_w <= 0.0f || cell_h <= 0.0f) return;
  // Visible cell range (inclusive), clamped to the grid.
  const int x0 = std::max(0, static_cast<int>(std::floor((cam_x - view_w * 0.5f - world.position.x) / cell_w)));
  const int x1 = std::min(map.width - 1, static_cast<int>(std::floor((cam_x + view_w * 0.5f - world.position.x) / cell_w)));
  const int y0 = std::max(0, static_cast<int>(std::floor((cam_y - view_h * 0.5f - world.position.y) / cell_h)));
  const int y1 = std::min(map.height - 1, static_cast<int>(std::floor((cam_y + view_h * 0.5f - world.position.y) / cell_h)));

  for (int layer = 0; layer < map.layer_count(); ++layer) {
    for (int cy = y0; cy <= y1; ++cy) {
      for (int cx = x0; cx <= x1; ++cx) {
        const int id = map.tile_at(layer, cx, cy);
        if (id <= 0) continue;
        renderer.draw(SpriteDraw{
            .texture = &texture,
            .x = world.position.x + (static_cast<float>(cx) + 0.5f) * cell_w,
            .y = world.position.y + (static_cast<float>(cy) + 0.5f) * cell_h,
            .width = cell_w,
            .height = cell_h,
            .tint = Color{1.0f, 1.0f, 1.0f, 1.0f},
            .uv = tile_uv(tileset, id),
        });
      }
    }
  }
}

}  // namespace

CameraView find_primary_camera(Scene& scene) {
  CameraView view;
  visit_scene_preorder(scene, [&](Node* node) {
    auto* camera = static_cast<Camera*>(node->find_component("Camera"));
    if (camera && camera->primary) {
      const auto world = node->world_transform();
      view = CameraView{world.position.x, world.position.y, camera->zoom};
    }
  });
  return view;
}

Vec2 viewport_to_world(Scene& scene, float px, float py, int width, int height) {
  const CameraView cam = find_primary_camera(scene);
  return Vec2{cam.x + (px - static_cast<float>(width) * 0.5f) / cam.zoom,
              cam.y + (py - static_cast<float>(height) * 0.5f) / cam.zoom};
}

bool paint_tile_at_world(Tilemap& map, const WorldTransform2D& node_world, Vec2 world, int layer, int tile_id) {
  int cx = 0, cy = 0;
  if (!map.world_to_cell(node_world, world, cx, cy)) return false;
  if (layer < 0 || layer >= map.layer_count()) return false;
  if (map.tile_at(layer, cx, cy) == tile_id) return false;
  return map.set_tile(layer, cx, cy, tile_id);
}

void preview_animations(Scene& scene, float dt) {
  visit_scene_preorder(scene, [&](Node* node) {
    if (auto* animated = static_cast<AnimatedSprite*>(node->find_component("AnimatedSprite"))) {
      animated->on_update(dt);
    }
  });
}

void render_scene(Scene& scene, SpriteRenderer& renderer, TextureCache& textures, int width, int height,
                   AnimationClipCache* clips, HudRenderer* hud_renderer, const Hud* hud, TilesetCache* tilesets) {
  const CameraView cam = find_primary_camera(scene);
  const float cam_x = cam.x, cam_y = cam.y, cam_zoom = cam.zoom;

  renderer.set_camera(cam_x, cam_y, cam_zoom);
  renderer.begin_frame(width, height, Color{0.12f, 0.12f, 0.16f, 1.0f});

  visit_scene_preorder(scene, [&](Node* node) {
    if (tilesets) {
      auto* map = static_cast<Tilemap*>(node->find_component("Tilemap"));
      const Tileset* tileset = map ? tilesets->get(map->tileset) : nullptr;
      Texture* tile_texture = tileset ? textures.get(tileset->texture) : nullptr;
      if (tile_texture) {
        draw_tilemap(*map, node->world_transform(), *tileset, *tile_texture, renderer, cam_x, cam_y,
                     static_cast<float>(width) / cam_zoom, static_cast<float>(height) / cam_zoom);
      }
    }
    auto* sprite = static_cast<Sprite*>(node->find_component("Sprite"));
    if (!sprite) return;
    Texture* texture = textures.get(sprite->texture);
    if (!texture) return;

    UvRect uv{};
    if (clips) {
      auto* animated = static_cast<AnimatedSprite*>(node->find_component("AnimatedSprite"));
      if (const AnimationClip* clip = animated ? clips->get(animated->clip) : nullptr) {
        if (!clip->frames.empty()) {
          uv = clip->frames[select_frame_index(*clip, animated->elapsed_seconds())].uv;
        }
      }
    }

    const auto world = node->world_transform();
    renderer.draw(SpriteDraw{
        .texture = texture,
        .x = world.position.x,
        .y = world.position.y,
        .width = sprite->width * world.scale.x,
        .height = sprite->height * world.scale.y,
        .rotation_radians = world.rotation_radians,
        .tint = sprite->tint,
        .uv = uv,
    });
  });

  if (hud_renderer && hud) hud_renderer->draw(*hud, renderer, width, height);

  renderer.end_frame();
}

}  // namespace arcane::editor
