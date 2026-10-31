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

void draw_particles(const ParticleEmitter& emitter, const WorldTransform2D& world, TextureCache& textures,
                    SpriteRenderer& renderer) {
  if (emitter.particles().empty()) return;
  Texture* texture = emitter.texture != 0 ? textures.get(emitter.texture) : nullptr;
  UvRect uv{};
  if (texture) {
    uv = emitter.frame_uv();
  } else {
    texture = textures.white();
  }
  if (!texture) return;
  for (const Particle& p : emitter.particles()) {
    const float t = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);
    const float size = emitter.start_size + (emitter.end_size - emitter.start_size) * t;
    const Color& a = emitter.start_color;
    const Color& b = emitter.end_color;
    renderer.draw(SpriteDraw{
        .texture = texture,
        .x = p.position.x + (emitter.world_space ? 0.0f : world.position.x),
        .y = p.position.y + (emitter.world_space ? 0.0f : world.position.y),
        .width = size,
        .height = size,
        .tint = Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t},
        .uv = uv,
    });
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

void preview_particles(Scene& scene, float dt) {
  visit_scene_preorder(scene, [&](Node* node) {
    if (auto* emitter = static_cast<ParticleEmitter*>(node->find_component("ParticleEmitter"))) {
      emitter->on_update(dt);
    }
  });
}

void restart_particles(Scene& scene) {
  visit_scene_preorder(scene, [&](Node* node) {
    if (auto* emitter = static_cast<ParticleEmitter*>(node->find_component("ParticleEmitter"))) {
      emitter->restart();
    }
  });
}

void render_scene(Scene& scene, SpriteRenderer& renderer, TextureCache& textures, int width, int height,
                   AnimationClipCache* clips, HudRenderer* hud_renderer, const Hud* hud, TilesetCache* tilesets, bool runtime_camera) {
  CameraView cam = find_primary_camera(scene);
  bool found = false;
  if (runtime_camera) visit_scene_preorder(scene, [&](Node* node) {
    auto* camera = static_cast<Camera2D*>(node->find_component("Camera2D"));
    if (!found && camera && camera->primary) {
      const Vec2 center = camera->view_center(width, height);
      cam = {center.x, center.y, camera->zoom > 0.0f ? camera->zoom : 1.0f};
      found = true;
    }
  });
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
    if (auto* emitter = static_cast<ParticleEmitter*>(node->find_component("ParticleEmitter"))) {
      draw_particles(*emitter, node->world_transform(), textures, renderer);
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

    if (sprite->flip_x) std::swap(uv.u0, uv.u1);
    if (sprite->flip_y) std::swap(uv.v0, uv.v1);

    const auto world = node->world_transform();
    SpriteDraw draw{
        .texture = texture,
        .x = world.position.x,
        .y = world.position.y,
        .width = sprite->width * world.scale.x,
        .height = sprite->height * world.scale.y,
        .rotation_radians = world.rotation_radians,
        .tint = sprite->tint,
        .uv = uv,
    };
    const auto* parallax = static_cast<ParallaxLayer*>(node->find_component("ParallaxLayer"));
    if (!parallax) {
      renderer.draw(draw);
      return;
    }
    // Shift by camera * (1 - factor), then repeat the sprite over the visible world rectangle.
    draw.x += cam_x * (1.0f - parallax->factor.x);
    draw.y += cam_y * (1.0f - parallax->factor.y);
    const float view_w = static_cast<float>(width) / cam_zoom, view_h = static_cast<float>(height) / cam_zoom;
    const float period_x = std::fabs(draw.width), period_y = std::fabs(draw.height);
    constexpr int kMaxRepeats = 256;  // guards against a tiny sprite on a huge view
    auto repeat_range = [&](bool repeat, float origin, float period, float cam, float view, int& first, int& last) {
      first = last = 0;
      if (!repeat || period < 1e-3f) return;
      first = static_cast<int>(std::floor((cam - view * 0.5f - origin) / period));
      last = static_cast<int>(std::floor((cam + view * 0.5f - origin) / period)) + 1;
      if (last - first > kMaxRepeats) last = first + kMaxRepeats;
    };
    int x0, x1, y0, y1;
    repeat_range(parallax->repeat_x, draw.x, period_x, cam_x, view_w, x0, x1);
    repeat_range(parallax->repeat_y, draw.y, period_y, cam_y, view_h, y0, y1);
    const float base_x = draw.x, base_y = draw.y;
    for (int iy = y0; iy <= y1; ++iy) {
      for (int ix = x0; ix <= x1; ++ix) {
        draw.x = base_x + static_cast<float>(ix) * period_x;
        draw.y = base_y + static_cast<float>(iy) * period_y;
        renderer.draw(draw);
      }
    }
  });

  if (hud_renderer && hud) hud_renderer->draw(*hud, renderer, width, height);

  renderer.end_frame();
}

}  // namespace arcane::editor
