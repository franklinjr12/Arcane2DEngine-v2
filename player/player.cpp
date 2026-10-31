#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "arcane/audio.hpp"
#include "arcane/game_context.hpp"
#include "arcane/debug.hpp"
#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "arcane/log.hpp"
#include "arcane/module.hpp"
#include "arcane/physics.hpp"
#include "arcane/renderer.hpp"
#include "arcane/replay.hpp"
#include "arcane/screenshot.hpp"
#include "arcane/time.hpp"
#include "arcane/window.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane_editor/audio_wiring.hpp"
#include "arcane_editor/project_runtime.hpp"
#include "arcane_editor/scene_render.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

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
  // Deterministic replay (--project mode, docs/formats/replay.md): --replay plays a recording one fixed step per
  // frame instead of reading the keyboard, then exits; --screenshot saves the last rendered frame as PNG;
  // --trace-out writes the per-frame state hashes; --expect-trace compares them (exit 3 on divergence).
  std::string replay_file;
  std::string screenshot_file;
  std::string trace_out_file;
  std::string expect_trace_file;
  // --headless (project mode only; needs --frames N or --replay FILE, incompatible with --screenshot): fixed-step
  // run with no window, GL, renderer or audio device. Hashes match the windowed --replay trace for the same build.
  bool headless = false;
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--frames" && i + 1 < argc) {
      args.frames = std::atoi(argv[++i]);
    } else if (arg == "--headless") {
      args.headless = true;
    } else if (arg == "--hidden") {
      args.hidden = true;
    } else if (arg == "--module" && i + 1 < argc) {
      args.module_path = argv[++i];
      args.project_module_override = args.module_path;
    } else if (arg == "--project" && i + 1 < argc) {
      args.project_dir = argv[++i];
    } else if (arg == "--replay" && i + 1 < argc) {
      args.replay_file = argv[++i];
    } else if (arg == "--screenshot" && i + 1 < argc) {
      args.screenshot_file = argv[++i];
    } else if (arg == "--trace-out" && i + 1 < argc) {
      args.trace_out_file = argv[++i];
    } else if (arg == "--expect-trace" && i + 1 < argc) {
      args.expect_trace_file = argv[++i];
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

  auto hud_renderer = arcane::HudRenderer::create();
  if (!hud_renderer) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create HUD renderer: {}", hud_renderer.error());
    return 1;
  }
  arcane::Hud hud = runtime->session().project().hud;  // authored layout; gameplay may add more
  arcane::set_active_hud(&hud);
  arcane::HudInput hud_input;

  arcane::editor::AnimationClipCache clips;
  clips.set_assets(arcane::editor::list_animation_clip_assets(runtime->session().assets_dir()));

  arcane::editor::AtlasCache atlases;
  atlases.set_assets(arcane::editor::list_animation_clip_assets(runtime->session().assets_dir()));
  arcane::editor::TilesetCache tilesets;
  tilesets.set_assets(arcane::editor::list_animation_clip_assets(runtime->session().assets_dir()));

  std::string audio_diagnostic;
  auto audio = arcane::AudioSystem::create_preferred(args.hidden, audio_diagnostic);
  if (!audio_diagnostic.empty()) {
    ARCANE_LOG_WARN(kLogCategory, "audio device unavailable; using null backend: {}", audio_diagnostic);
  }
  if (!audio) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create audio system: {}", audio.error());
    return 1;
  }
  arcane::set_active_audio_system(&*audio);
  arcane::GameContext game_context(runtime->session().project_dir(), runtime->registry());
  arcane::set_active_game_context(&game_context);
  arcane::editor::start_play_on_start_sounds(runtime->session().scene(), *audio,
                                              arcane::editor::list_audio_assets(runtime->session().assets_dir()));

  std::optional<arcane::ReplayInput> replay;
  if (!args.replay_file.empty()) {
    auto text = arcane::read_text_file(args.replay_file);
    auto recording = text ? arcane::load_recording_json(*text) : std::unexpected(text.error());
    if (!recording) {
      ARCANE_LOG_ERROR(kLogCategory, "cannot use replay '{}': {}", args.replay_file, recording.error());
      return 1;
    }
    replay.emplace(std::move(*recording));
    arcane::apply_replay_seed(runtime->session().scene(), runtime->registry(), replay->recording().seed);
  }
  std::vector<std::uint64_t> trace;

  arcane::PhysicsWorld physics;
  physics.rebuild(runtime->session().scene());
  game_context.set_physics(&physics);
  game_context.set_collision_layers(runtime->session().project().collision_layers);

  arcane::InputActionMap input_map = runtime->session().project().input_actions;
  arcane::GlfwInputBackend input_backend(*window);
  arcane::set_active_input_map(&input_map);

  arcane::FixedTimestep timestep(60.0);
  auto previous_time = std::chrono::steady_clock::now();
  long frame_count = 0;

  // Debug overlay: starts as authored in project.json ("debug"), F3 toggles it.
  const arcane::DebugConfig debug_config = runtime->session().project().debug;
  bool overlay_visible = debug_config.overlay_visible;
  bool overlay_key_was_down = false;
  arcane::FrameTimer frame_timer;
  arcane::Hud overlay_hud;

  double asset_poll_accum = 0.0;
  int exit_code = 0;
  while (!window->should_close()) {
    if (args.frames.has_value() && frame_count >= *args.frames) break;
    if (replay && replay->finished()) break;

    const auto now = std::chrono::steady_clock::now();
    const double delta_seconds = std::chrono::duration<double>(now - previous_time).count();
    previous_time = now;
    frame_timer.tick(delta_seconds);
    // Hot-reload edited clips and images twice a second (not during replays, which must be reproducible).
    asset_poll_accum += delta_seconds;
    if (!replay && asset_poll_accum >= 0.5) {
      asset_poll_accum = 0.0;
      const auto clip_assets = arcane::editor::list_animation_clip_assets(runtime->session().assets_dir());
      textures.set_assets(arcane::editor::list_image_assets(runtime->session().assets_dir()));
      clips.set_assets(clip_assets);
      tilesets.set_assets(clip_assets);
      atlases.set_assets(clip_assets);
      textures.refresh();
      clips.refresh();
      tilesets.refresh();
      atlases.refresh();
    }
    const bool overlay_key_down = input_backend.is_down(arcane::InputDevice::Keyboard, arcane::kDebugOverlayKey);
    if (overlay_key_down && !overlay_key_was_down) overlay_visible = !overlay_visible;
    overlay_key_was_down = overlay_key_down;

    // HUD sees pointer/confirm input first; what it consumes is hidden from gameplay actions.
    int fb_width = 0, fb_height = 0;
    window->framebuffer_size(fb_width, fb_height);
    int steps = 0;
    float step_dt = static_cast<float>(timestep.fixed_delta_seconds());
    if (replay) {
      input_map.update(replay->advance());  // recorded input only; the keyboard is ignored
      steps = 1;
      step_dt = static_cast<float>(1.0 / replay->recording().fixed_hz);
    } else {
      hud_input.update(hud, input_backend, fb_width, fb_height);
      input_map.update(hud_input.gameplay_backend());
      steps = timestep.advance(delta_seconds);
    }
    arcane::Scene& scene = runtime->session().scene();
    game_context.set_viewport_size(fb_width, fb_height);
    for (int i = 0; i < steps; ++i) {
      if (runtime->module_api()) runtime->module_api()->on_update(step_dt);
      scene.update(step_dt);
      physics.step(scene, step_dt);
      if (game_context.take_restart_request()) {
        auto reloaded = arcane::reload_scene_file(scene, runtime->session().main_scene_path(), runtime->registry());
        if (reloaded) physics.rebuild(scene);
        else ARCANE_LOG_ERROR(kLogCategory, "scene restart failed: {}", reloaded.error());
      }
    }
    if (replay) trace.push_back(arcane::hash_scene_state(scene, runtime->registry()));

    arcane::editor::render_scene(scene, *renderer, textures, fb_width, fb_height, &clips, &*hud_renderer, &hud,
                                &tilesets, true, &atlases);
    if (runtime->module_api()) runtime->module_api()->on_render();

    if (overlay_visible) {
      const arcane::RenderStats& render_stats = renderer->stats();
      arcane::FrameStats stats{frame_timer.fps(), frame_timer.frame_ms(), render_stats.draw_calls,
                               render_stats.sprites, physics.body_count(), arcane::count_particles(scene)};
      arcane::fill_debug_overlay_hud(overlay_hud, stats, debug_config);
      hud_renderer->draw(overlay_hud, *renderer, fb_width, fb_height);
      renderer->flush();
    }

    if (replay && replay->finished() && !args.screenshot_file.empty()) {
      auto image = arcane::capture_framebuffer(fb_width, fb_height);
      auto saved = image ? arcane::write_png(args.screenshot_file, *image) : std::unexpected(image.error());
      if (!saved) {
        ARCANE_LOG_ERROR(kLogCategory, "screenshot failed: {}", saved.error());
        exit_code = 1;
      }
    }

    window->swap_buffers();
    window->poll_events();
    ++frame_count;
  }

  if (replay) {
    if (!args.trace_out_file.empty()) {
      auto text = arcane::save_trace_json(trace);
      auto written = text ? arcane::write_text_file_atomic(args.trace_out_file, *text) : std::unexpected(text.error());
      if (!written) {
        ARCANE_LOG_ERROR(kLogCategory, "cannot write trace: {}", written.error());
        exit_code = 1;
      }
    }
    if (!args.expect_trace_file.empty()) {
      auto text = arcane::read_text_file(args.expect_trace_file);
      auto expected = text ? arcane::load_trace_json(*text) : std::unexpected(text.error());
      if (!expected) {
        ARCANE_LOG_ERROR(kLogCategory, "cannot use trace '{}': {}", args.expect_trace_file, expected.error());
        exit_code = 1;
      } else if (auto frame = arcane::first_divergent_frame(*expected, trace)) {
        ARCANE_LOG_ERROR(kLogCategory, "replay diverged: first divergent frame is {}", *frame);
        exit_code = 3;
      }
    }
  }

  arcane::set_active_game_context(nullptr);
  arcane::set_active_audio_system(nullptr);
  arcane::set_active_hud(nullptr);
  ARCANE_LOG_INFO(kLogCategory, "exiting after {} frames", frame_count);
  return exit_code;
}

// Display-free counterpart of run_project: same project/module/input map, fixed steps through
// arcane::run_fixed_frames, audio on the null backend, nothing rendered.
int run_project_headless(const Args& args) {
  auto runtime = arcane::editor::ProjectRuntime::open(args.project_dir, args.project_module_override);
  if (!runtime) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to open project '{}': {}", args.project_dir, runtime.error());
    return 1;
  }
  auto audio = arcane::AudioSystem::create(true);
  if (!audio) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create audio system: {}", audio.error());
    return 1;
  }
  arcane::set_active_audio_system(&*audio);
  arcane::GameContext game_context(runtime->session().project_dir(), runtime->registry());
  arcane::set_active_game_context(&game_context);
  arcane::Hud hud = runtime->session().project().hud;  // authored layout; gameplay may add more, never drawn
  arcane::set_active_hud(&hud);

  std::optional<arcane::ReplayRecording> recording;
  if (!args.replay_file.empty()) {
    auto text = arcane::read_text_file(args.replay_file);
    auto loaded = text ? arcane::load_recording_json(*text) : std::unexpected(text.error());
    if (!loaded) {
      ARCANE_LOG_ERROR(kLogCategory, "cannot use replay '{}': {}", args.replay_file, loaded.error());
      arcane::set_active_game_context(nullptr);
      arcane::set_active_audio_system(nullptr);
      arcane::set_active_hud(nullptr);
      return 1;
    }
    recording = std::move(*loaded);
  }

  arcane::InputActionMap input_map = runtime->session().project().input_actions;
  arcane::FixedRunOptions options;
  if (recording) {
    options.recording = &*recording;
  } else {
    options.frames = *args.frames;
  }
  if (runtime->module_api()) {
    options.before_step = [&](float dt) { runtime->module_api()->on_update(dt); };
  }
  if (auto authored = arcane::read_text_file(runtime->session().main_scene_path())) {
    options.restart_scene_json = std::move(*authored);
  }
  auto result = arcane::run_fixed_frames(runtime->session().scene(), runtime->registry(), input_map, options);
  int exit_code = 0;
  long frame_count = 0;
  if (!result) {
    ARCANE_LOG_ERROR(kLogCategory, "headless run failed: {}", result.error());
    exit_code = 1;
  } else {
    frame_count = static_cast<long>(result->size());
    const std::vector<std::uint64_t>& trace = *result;
    if (!args.trace_out_file.empty()) {
      auto text = arcane::save_trace_json(trace);
      auto written = text ? arcane::write_text_file_atomic(args.trace_out_file, *text) : std::unexpected(text.error());
      if (!written) {
        ARCANE_LOG_ERROR(kLogCategory, "cannot write trace: {}", written.error());
        exit_code = 1;
      }
    }
    if (!args.expect_trace_file.empty()) {
      auto text = arcane::read_text_file(args.expect_trace_file);
      auto expected = text ? arcane::load_trace_json(*text) : std::unexpected(text.error());
      if (!expected) {
        ARCANE_LOG_ERROR(kLogCategory, "cannot use trace '{}': {}", args.expect_trace_file, expected.error());
        exit_code = 1;
      } else if (auto frame = arcane::first_divergent_frame(*expected, trace)) {
        ARCANE_LOG_ERROR(kLogCategory, "replay diverged: first divergent frame is {}", *frame);
        exit_code = 3;
      }
    }
  }
  arcane::set_active_game_context(nullptr);
  arcane::set_active_audio_system(nullptr);
  arcane::set_active_hud(nullptr);
  ARCANE_LOG_INFO(kLogCategory, "exiting after {} frames", frame_count);
  return exit_code;
}

// Directory holding the running executable, so an exported game finds its
// project without depending on the current working directory.
std::filesystem::path executable_dir() {
#if defined(_WIN32)
  wchar_t buffer[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return {};
  return std::filesystem::path(buffer).parent_path();
#else
  std::error_code ec;
  const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::filesystem::path{} : exe.parent_path();
#endif
}

}  // namespace

int main(int argc, char** argv) {
  Args args = parse_args(argc, argv);
  // Exported layout: <exe dir>/game/project.json is run when no --project is given.
  if (args.project_dir.empty()) {
    const auto exported = executable_dir() / "game";
    if (std::filesystem::exists(exported / "project.json")) args.project_dir = exported.string();
  }
  if (args.headless) {
    if (args.project_dir.empty()) {
      ARCANE_LOG_ERROR(kLogCategory, "--headless needs --project (or an exported game/ next to the executable)");
      return 2;
    }
    if (!args.frames.has_value() && args.replay_file.empty()) {
      ARCANE_LOG_ERROR(kLogCategory, "--headless needs --frames N or --replay FILE");
      return 2;
    }
    if (!args.screenshot_file.empty()) {
      ARCANE_LOG_ERROR(kLogCategory, "--screenshot needs a window and cannot be combined with --headless");
      return 2;
    }
    return run_project_headless(args);
  }
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
