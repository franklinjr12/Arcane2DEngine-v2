// render: draws the (stepped) scene into an offscreen framebuffer and writes a PNG (docs/commands.md). Needs a GL
// context (a display or Xvfb); without one it fails with RENDER_UNAVAILABLE (exit 8) instead of crashing.
#include <glad/gl.h>

#include <charconv>
#include <filesystem>

#include "arcane/hud.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/renderer.hpp"
#include "arcane/replay.hpp"
#include "arcane/screenshot.hpp"
#include "arcane/window.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_editor/scene_render.hpp"
#include "arcane_tooling/context.hpp"
#include "offline_internal.hpp"

namespace arcane::tooling {

namespace {

using Result = std::expected<Json, ToolError>;

std::expected<std::pair<int, int>, ToolError> parse_size(const std::string& text) {
  const auto x = text.find('x');
  int w = 0, h = 0;
  bool ok = x != std::string::npos && x > 0 && x + 1 < text.size();
  if (ok) {
    ok = std::from_chars(text.data(), text.data() + x, w).ec == std::errc{} &&
         std::from_chars(text.data() + x + 1, text.data() + text.size(), h).ec == std::errc{} && w >= 1 && h >= 1 &&
         w <= 4096 && h <= 4096;
  }
  if (!ok) return std::unexpected(offline_error("ARGS_INVALID", "size must be WxH with 1..4096 per side, got '" + text + "'", "", "/size"));
  return std::pair{w, h};
}

void select_camera(Node& node, Node* chosen) {
  for (const char* type : {"Camera", "Camera2D"}) {
    if (Component* c = node.find_component(type)) {
      const bool primary = &node == chosen;
      if (std::string_view(type) == "Camera") static_cast<Camera*>(c)->primary = primary;
      else static_cast<Camera2D*>(c)->primary = primary;
    }
  }
  for (const auto& child : node.children()) select_camera(*child, chosen);
}

Result render(const OfflineEnv& env, const Json& args, bool dry_run) {
  const std::string output = args["output"].get<std::string>();
  if (auto ok = offline_check_path(output, "output"); !ok) return std::unexpected(ok.error());
  std::string compare;
  if (args.contains("compare")) {
    compare = args["compare"].get<std::string>();
    if (auto ok = offline_check_path(compare, "compare"); !ok) return std::unexpected(ok.error());
  }
  std::optional<std::pair<int, int>> explicit_size;
  if (args.contains("size")) {
    auto parsed = parse_size(args["size"].get<std::string>());
    if (!parsed) return std::unexpected(parsed.error());
    explicit_size = *parsed;
  }
  std::vector<ImageMaskRect> masks;
  if (args.contains("mask")) {
    for (const auto& r : args["mask"]) masks.push_back({r[0].get<int>(), r[1].get<int>(), r[2].get<int>(), r[3].get<int>()});
  }
  auto context = ProjectContext::open(env.project_dir, env.module_override, env.scene_override);
  if (!context) return std::unexpected(context.error());
  Node* camera = nullptr;
  if (args.contains("camera")) {
    auto node = resolve_node(**context, args["camera"].get<std::string>());
    if (!node) return std::unexpected(node.error());
    if (!(*node)->find_component("Camera") && !(*node)->find_component("Camera2D")) {
      return std::unexpected(offline_error("COMPONENT_NOT_FOUND", "node has no Camera or Camera2D component", "", "/camera"));
    }
    camera = *node;
  }
  const NodeId camera_id = camera ? camera->id() : 0;
  // Explicit --size wins, then the project's design resolution, then 320x180. The image is the logical view (no bars).
  const DisplayConfig& display = (*context)->session().project().display;
  const bool has_design = display.design_width > 0 && display.design_height > 0;
  const auto size = std::optional<std::pair<int, int>>(
      explicit_size ? *explicit_size : has_design ? std::pair{display.design_width, display.design_height} : std::pair{320, 180});

  // GL context first: without one nothing else is worth doing. The window outlives every GL object below.
  auto window = Window::create({.width = 64, .height = 64, .title = "arcane render", .vsync = false, .hidden = true});
  if (!window || !load_gl_functions()) {
    return std::unexpected(offline_error("RENDER_UNAVAILABLE", "no GL-capable display: " + (window ? std::string("GL functions did not load") : window.error()), "", "",
                                         "run under a display or `xvfb-run -a arcane ...`"));
  }
  auto sprites = SpriteRenderer::create();
  auto hud_renderer = HudRenderer::create();
  if (!sprites || !hud_renderer) {
    return std::unexpected(offline_error("RENDER_UNAVAILABLE", "renderer setup failed: " + (sprites ? hud_renderer.error() : sprites.error())));
  }
  const auto assets = (*context)->session().assets_dir();
  editor::TextureCache textures;
  textures.set_assets(editor::list_image_assets(assets));
  editor::AnimationClipCache clips;
  clips.set_assets(editor::list_animation_clip_assets(assets));
  editor::AtlasCache atlases;
  atlases.set_assets(editor::list_animation_clip_assets(assets));
  editor::TilesetCache tilesets;
  tilesets.set_assets(editor::list_animation_clip_assets(assets));

  FixedRunOptions options;
  options.frames = args.value("frames", 0);
  options.fixed_hz = args.value("fixed_hz", 60);
  options.seed = args.value("seed", 0);
  options.viewport_width = size->first;
  options.viewport_height = size->second;
  std::optional<ImageRgba> image;
  std::string failure;
  auto out = run_on_copy(**context, options, "", [&](Scene& scene, Hud& hud) -> std::expected<void, ToolError> {
    if (camera_id != 0) {
      Node* chosen = scene.find_node(camera_id);
      for (const auto& root : scene.roots()) select_camera(*root, chosen);
    }
    auto drawn = render_offscreen(size->first, size->second, [&] {
      editor::render_scene(scene, *sprites, textures, size->first, size->second, &clips, &*hud_renderer, &hud, &tilesets, true, &atlases);
    });
    if (!drawn) return std::unexpected(offline_error("RENDER_UNAVAILABLE", drawn.error()));
    image = std::move(*drawn);
    return {};
  });
  if (!out) return std::unexpected(out.error());

  if (!dry_run) {
    const auto target = env.project_dir / output;
    if (auto ok = write_png(target, *image); !ok) return std::unexpected(offline_error("IO_ERROR", ok.error(), output));
  }
  const char* renderer_name = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  Json result = {{"output", output},
                 {"scene", (*context)->scene_file()},
                 {"frame", options.frames},
                 {"fixed_hz", options.fixed_hz},
                 {"size", {{"width", size->first}, {"height", size->second}}},
                 {"scene_hash", hash_to_hex(out->hashes.back())},
                 {"camera", args.contains("camera") ? args["camera"] : Json(nullptr)},
                 {"backend", {{"api", "opengl-3.3-offscreen-fbo"}, {"renderer", renderer_name ? renderer_name : "unknown"}}},
                 {"module", out->module},
                 {"compare", nullptr}};
  if (!compare.empty()) {
    auto golden = read_png(env.project_dir / compare);
    if (!golden) return std::unexpected(offline_error("IO_ERROR", golden.error(), compare));
    const int tolerance = args.value("tolerance", 2);
    const ImageDiff diff = compare_images(*image, *golden, tolerance, masks);
    const bool same = diff.same_size && diff.differing_pixels == 0;
    result["compare"] = {{"golden", compare}, {"same_size", diff.same_size}, {"tolerance", tolerance},
                         {"max_channel_delta", diff.max_channel_delta}, {"differing_pixels", diff.differing_pixels},
                         {"masks", masks.size()}, {"match", same}};
    result["ok"] = same;  // false = reported failure (exit 4)
  }
  return result;
}

}  // namespace

std::vector<OfflineCommand> make_render_commands() {
  const Json kStr = {{"type", "string"}};
  std::vector<OfflineCommand> c;
  c.push_back({{"render",
                "Render the main (or --scene) scene to a PNG through an offscreen GL framebuffer: advance --frames fixed steps on a scene copy "
                "(module on_update included, default 0), draw it with the scene renderer and HUD, write output. camera selects a Camera/Camera2D node, "
                "size is WxH (default: the project's display design resolution, else 320x180). With compare the image is checked against a golden PNG (per-channel tolerance, default 2; mask lists "
                "[x,y,w,h] pixel rectangles to ignore); a mismatch is ok=false (exit 4). Needs a display or Xvfb, else RENDER_UNAVAILABLE (exit 8). "
                "The result has the frame, size, scene_hash and backend. --dry-run renders but writes nothing.",
                offline_object_schema({{"output", kStr},
                                       {"size", kStr},
                                       {"frames", {{"type", "integer"}, {"minimum", 0}, {"maximum", 1000000}}},
                                       {"fixed_hz", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}},
                                       {"seed", {{"type", "integer"}, {"minimum", 0}}},
                                       {"camera", kStr},
                                       {"compare", kStr},
                                       {"tolerance", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}},
                                       {"mask", {{"type", "array"},
                                                 {"items", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}},
                                                            {"minItems", 4}, {"maxItems", 4}}}}}},
                                      {"output"}),
                {{"type", "object"}},
                true},
               render});
  return c;
}

}  // namespace arcane::tooling
