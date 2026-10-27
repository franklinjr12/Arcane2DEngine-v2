#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "arcane/input.hpp"
#include "arcane/log.hpp"
#include "arcane/module.hpp"
#include "arcane/physics.hpp"
#include "arcane/renderer.hpp"
#include "arcane/time.hpp"
#include "arcane/window.hpp"
#include "arcane_editor/project_runtime.hpp"
#include "arcane_editor/scene_render.hpp"

namespace {
constexpr std::string_view kLogCategory = "player";

struct Args {
  std::optional<int> frames;
  bool hidden = false;
  std::string module_path = ARCANE_DEFAULT_MODULE_PATH;
  // If set, runs the on-disk project standalone (arcane_editor minus the
  // GUI: no hierarchy/inspector/Play-Stop, the scene just always runs)
  // instead of the checkerboard-texture smoke demo below.
  std::string project_dir;
  // --module override for --project mode. Deliberately separate from
  // module_path (whose default equals ARCANE_DEFAULT_MODULE_PATH, which
  // CTest also passes via --module for the demo mode): comparing the CLI
  // value against that default to decide "was --module passed" breaks
  // when they happen to be equal, see editor_main.cpp's module_override.
  std::string project_module_override;
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--frames" && i + 1 < argc) {
      args.frames = std::atoi(argv[++i]);
    } else if (arg == "--hidden") {
      args.hidden = true;
    } else if (arg == "--module" && i + 1 < argc) {
      args.module_path = argv[++i];
      args.project_module_override = args.module_path;
    } else if (arg == "--project" && i + 1 < argc) {
      args.project_dir = argv[++i];
    }
  }
  return args;
}

// 2x2 magenta/white checkerboard, generated in-process so the player does
// not depend on a committed binary asset for this smoke test.
std::vector<unsigned char> make_checkerboard_pixels(int size) {
  std::vector<unsigned char> pixels(static_cast<size_t>(size) * size * 4);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const bool even = ((x / (size / 2)) + (y / (size / 2))) % 2 == 0;
      unsigned char* p = &pixels[(static_cast<size_t>(y) * size + x) * 4];
      p[0] = even ? 255 : 30;
      p[1] = even ? 0 : 30;
      p[2] = even ? 255 : 30;
      p[3] = 255;
    }
  }
  return pixels;
}

// Runs an on-disk project standalone: no editor UI, the scene just ticks
// and renders every frame (there is no Play/Stop mode here -- the player
// *is* always in "Play"). Shares ProjectRuntime/TextureCache/render_scene
// with arcane_editor so project loading and scene rendering behave
// identically in both.
int run_project(const Args& args) {
  auto window = arcane::Window::create(
      {.width = 1280, .height = 800, .title = "Arcane2D Player", .hidden = args.hidden});
  if (!window) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create window: {}", window.error());
    return 77;  // no GL-capable display; CTest SKIP convention, see test_renderer_gl.
  }
  if (!arcane::load_gl_functions()) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to load OpenGL functions");
    return 1;
  }

  auto runtime = arcane::editor::ProjectRuntime::open(args.project_dir, args.project_module_override);
  if (!runtime) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to open project '{}': {}", args.project_dir, runtime.error());
    return 1;
  }

  auto renderer = arcane::SpriteRenderer::create();
  if (!renderer) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create sprite renderer: {}", renderer.error());
    return 1;
  }

  arcane::editor::TextureCache textures;
  textures.set_assets(arcane::editor::list_image_assets(runtime->session().assets_dir()));

  arcane::PhysicsWorld physics;
  physics.rebuild(runtime->session().scene());

  arcane::InputActionMap input_map = runtime->session().project().input_actions;
  arcane::GlfwInputBackend input_backend(*window);
  arcane::set_active_input_map(&input_map);

  arcane::FixedTimestep timestep(60.0);
  auto previous_time = std::chrono::steady_clock::now();
  long frame_count = 0;

  while (!window->should_close()) {
    if (args.frames.has_value() && frame_count >= *args.frames) break;

    const auto now = std::chrono::steady_clock::now();
    const double delta_seconds = std::chrono::duration<double>(now - previous_time).count();
    previous_time = now;

    input_map.update(input_backend);

    const int steps = timestep.advance(delta_seconds);
    arcane::Scene& scene = runtime->session().scene();
    for (int i = 0; i < steps; ++i) {
      if (runtime->module_api()) runtime->module_api()->on_update(static_cast<float>(timestep.fixed_delta_seconds()));
      scene.update(static_cast<float>(timestep.fixed_delta_seconds()));
      physics.step(scene, static_cast<float>(timestep.fixed_delta_seconds()));
    }

    int fb_width = 0, fb_height = 0;
    window->framebuffer_size(fb_width, fb_height);
    arcane::editor::render_scene(scene, *renderer, textures, fb_width, fb_height);
    if (runtime->module_api()) runtime->module_api()->on_render();

    window->swap_buffers();
    window->poll_events();
    ++frame_count;
  }

  ARCANE_LOG_INFO(kLogCategory, "exiting after {} frames", frame_count);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = parse_args(argc, argv);
  if (!args.project_dir.empty()) {
    return run_project(args);
  }

  auto window = arcane::Window::create({.width = 640, .height = 480, .title = "Arcane2D Player", .hidden = args.hidden});
  if (!window) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create window: {}", window.error());
    // No GL-capable display/driver available (e.g. headless CI without
    // Xvfb): signal CTest's SKIP_RETURN_CODE rather than a hard failure.
    return 77;
  }
  if (!arcane::load_gl_functions()) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to load OpenGL functions");
    return 1;
  }

  auto renderer = arcane::SpriteRenderer::create();
  if (!renderer) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create sprite renderer: {}", renderer.error());
    return 1;
  }

  const auto pixels = make_checkerboard_pixels(64);
  auto texture = arcane::Texture::load_from_pixels(pixels, 64, 64, arcane::TextureFilter::Nearest);
  if (!texture) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create texture: {}", texture.error());
    return 1;
  }

  auto module = arcane::GameModule::load(args.module_path);
  if (!module) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to load game module {}: {}", args.module_path, module.error());
    return 1;
  }
  module->api()->on_load();

  arcane::FixedTimestep timestep(60.0);
  auto previous_time = std::chrono::steady_clock::now();
  long frame_count = 0;

  while (!window->should_close()) {
    if (args.frames.has_value() && frame_count >= *args.frames) {
      break;
    }

    const auto now = std::chrono::steady_clock::now();
    const double delta_seconds = std::chrono::duration<double>(now - previous_time).count();
    previous_time = now;

    const int steps = timestep.advance(delta_seconds);
    for (int i = 0; i < steps; ++i) {
      module->api()->on_update(static_cast<float>(timestep.fixed_delta_seconds()));
    }

    int fb_width = 0, fb_height = 0;
    window->framebuffer_size(fb_width, fb_height);
    renderer->set_camera(0.0f, 0.0f, 1.0f);
    renderer->begin_frame(fb_width, fb_height, arcane::Color{0.1f, 0.1f, 0.15f, 1.0f});
    renderer->draw(arcane::SpriteDraw{.texture = &*texture, .x = 0.0f, .y = 0.0f, .width = 128.0f, .height = 128.0f});
    module->api()->on_render();
    renderer->end_frame();

    window->swap_buffers();
    window->poll_events();
    ++frame_count;
  }

  module->api()->on_unload();
  ARCANE_LOG_INFO(kLogCategory, "exiting after {} frames", frame_count);
  return 0;
}
