// arcane_editor: the Foundation C minimal integrated editor. Dear ImGui
// (docking) + GLFW/OpenGL3, built on top of arcane_editor_core (selection,
// Play/Stop isolation, project session) and arcane_runtime (scene, sprite
// renderer, component registry). See docs/decisions.md for the viewport
// offscreen-framebuffer and asset-id design choices.
#include <glad/gl.h>

#include <GLFW/glfw3.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // glfw3native.h pulls <windows.h>; keep std::min/max usable
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "arcane_editor/clip_authoring.hpp"

#include "arcane/audio.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "arcane/log.hpp"
#include "arcane/node.hpp"
#include "arcane/renderer.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"
#include "arcane/time.hpp"
#include "arcane/window.hpp"
#include "arcane_editor/audio_wiring.hpp"
#include "arcane_editor/editor_state.hpp"
#include "arcane_editor/project_runtime.hpp"
#include "arcane_editor/scene_render.hpp"
#include "arcane_editor/tile_paint.hpp"

namespace {

constexpr std::string_view kLogCategory = "editor";

struct Args {
  std::string project_dir;
  std::optional<int> frames;
  std::optional<int> play_frames;
  bool hidden = false;
  // Overrides project.json's "module" field; used by the gl-labelled CTest
  // entry so each build config (gcc/clang+ASan/...) loads the module it
  // just built rather than a single shared copy in the source tree that
  // the wrong config could have last overwritten (see docs/decisions.md).
  std::string module_override;
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--project" && i + 1 < argc) {
      args.project_dir = argv[++i];
    } else if (arg == "--frames" && i + 1 < argc) {
      args.frames = std::atoi(argv[++i]);
    } else if (arg == "--play-frames" && i + 1 < argc) {
      args.play_frames = std::atoi(argv[++i]);
    } else if (arg == "--module" && i + 1 < argc) {
      args.module_override = argv[++i];
    } else if (arg == "--hidden") {
      args.hidden = true;
    }
  }
  return args;
}

// Owns an offscreen color-texture framebuffer the viewport panel renders the
// scene into, then displays via ImGui::Image. Recreated whenever the panel
// is resized.
class ViewportTarget {
 public:
  ~ViewportTarget() { destroy(); }

  void ensure_size(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == width_ && height == height_ && fbo_ != 0) return;
    destroy();
    width_ = width;
    height_ = height;

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void bind() const { glBindFramebuffer(GL_FRAMEBUFFER, fbo_); }
  static void unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

  std::uint32_t texture() const noexcept { return texture_; }
  int width() const noexcept { return width_; }
  int height() const noexcept { return height_; }

 private:
  void destroy() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (texture_) glDeleteTextures(1, &texture_);
    fbo_ = 0;
    texture_ = 0;
  }

  std::uint32_t fbo_ = 0;
  std::uint32_t texture_ = 0;
  int width_ = 0;
  int height_ = 0;
};

bool draw_property_editor(const arcane::PropertyDescriptor& descriptor, arcane::Component& component) {
  arcane::PropertyValue value = descriptor.get(component);
  bool changed = false;
  switch (descriptor.type) {
    case arcane::PropertyType::Bool: {
      bool v = std::get<bool>(value);
      changed = ImGui::Checkbox(descriptor.name.c_str(), &v);
      if (changed) value = v;
      break;
    }
    case arcane::PropertyType::Int: {
      int v = std::get<int>(value);
      changed = ImGui::InputInt(descriptor.name.c_str(), &v);
      if (changed) value = v;
      break;
    }
    case arcane::PropertyType::Float: {
      float v = std::get<float>(value);
      changed = ImGui::DragFloat(descriptor.name.c_str(), &v, 0.1f);
      if (changed) value = v;
      break;
    }
    case arcane::PropertyType::Vec2: {
      arcane::Vec2 v = std::get<arcane::Vec2>(value);
      float arr[2] = {v.x, v.y};
      changed = ImGui::DragFloat2(descriptor.name.c_str(), arr, 0.1f);
      if (changed) value = arcane::Vec2{arr[0], arr[1]};
      break;
    }
    case arcane::PropertyType::Color: {
      arcane::Color c = std::get<arcane::Color>(value);
      float arr[4] = {c.r, c.g, c.b, c.a};
      changed = ImGui::ColorEdit4(descriptor.name.c_str(), arr);
      if (changed) value = arcane::Color{arr[0], arr[1], arr[2], arr[3]};
      break;
    }
    case arcane::PropertyType::String: {
      std::string v = std::get<std::string>(value);
      char buffer[256];
      std::snprintf(buffer, sizeof(buffer), "%s", v.c_str());
      changed = ImGui::InputText(descriptor.name.c_str(), buffer, sizeof(buffer));
      if (changed) value = std::string(buffer);
      break;
    }
    case arcane::PropertyType::AssetId:
    case arcane::PropertyType::EntityId: {
      arcane::AssetId v = std::get<arcane::AssetId>(value);
      // Raw numeric id editor; Assets panel also assigns these by click.
      long long raw = static_cast<long long>(v);
      changed = ImGui::InputScalar(descriptor.name.c_str(), ImGuiDataType_S64, &raw);
      if (changed) value = static_cast<arcane::AssetId>(raw);
      break;
    }
  }
  if (changed) {
    descriptor.set(component, value);
  }
  return changed;
}

// Inspector-style editor for the project's authored HUD: add/remove elements and
// edit their fields. `changed` is called after any edit.
void draw_hud_editor(arcane::Hud& hud, const std::function<void()>& changed) {
  static constexpr const char* kAnchors[] = {"TopLeft", "Top", "TopRight", "Left", "Center",
                                             "Right", "BottomLeft", "Bottom", "BottomRight"};
  auto text_field = [&](const char* label, std::string& value) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    if (ImGui::InputText(label, buf, sizeof(buf))) {
      value = buf;
      changed();
    }
  };
  auto layout = [&](arcane::HudAnchor& anchor, float& ox, float& oy) {
    int a = static_cast<int>(anchor);
    if (ImGui::Combo("Anchor", &a, kAnchors, 9)) {
      anchor = static_cast<arcane::HudAnchor>(a);
      changed();
    }
    float off[2] = {ox, oy};
    if (ImGui::DragFloat2("Offset", off, 0.5f)) {
      ox = off[0];
      oy = off[1];
      changed();
    }
  };
  auto size_field = [&](float& w, float& h) {
    float v[2] = {w, h};
    if (ImGui::DragFloat2("Size", v, 0.5f, 1.0f, 4000.0f)) {
      w = v[0];
      h = v[1];
      changed();
    }
  };
  auto color_field = [&](const char* label, arcane::Color& c) {
    if (ImGui::ColorEdit4(label, &c.r)) changed();
  };

  if (ImGui::Button("+ Button")) {
    hud.add(arcane::HudButton{.id = "button", .label = "Button"});
    changed();
  }
  ImGui::SameLine();
  if (ImGui::Button("+ Text")) {
    hud.add(arcane::HudText{.text = "Text", .color = {1, 1, 1, 1}});
    changed();
  }
  ImGui::SameLine();
  if (ImGui::Button("+ Bar")) {
    hud.add(arcane::HudProgressBar{});
    changed();
  }

  auto& elements = hud.elements();
  int remove = -1;
  for (int i = 0; i < static_cast<int>(elements.size()); ++i) {
    ImGui::PushID(i);
    ImGui::Separator();
    if (auto* b = std::get_if<arcane::HudButton>(&elements[i])) {
      ImGui::Text("Button");
      text_field("Id", b->id);
      text_field("Label", b->label);
      layout(b->anchor, b->offset_x, b->offset_y);
      size_field(b->width, b->height);
      if (ImGui::DragFloat("Font scale", &b->font_scale, 0.1f, 0.5f, 16.0f)) changed();
      if (ImGui::Checkbox("Enabled", &b->enabled)) changed();
      color_field("Normal", b->normal);
      color_field("Hover", b->hover);
      color_field("Pressed", b->pressed);
      color_field("Disabled", b->disabled);
      color_field("Text color", b->text_color);
    } else if (auto* t = std::get_if<arcane::HudText>(&elements[i])) {
      ImGui::Text("Text");
      text_field("Text", t->text);
      layout(t->anchor, t->offset_x, t->offset_y);
      if (ImGui::DragFloat("Font scale", &t->font_scale, 0.1f, 0.5f, 16.0f)) changed();
      color_field("Color", t->color);
    } else if (auto* bar = std::get_if<arcane::HudProgressBar>(&elements[i])) {
      ImGui::Text("Progress bar");
      layout(bar->anchor, bar->offset_x, bar->offset_y);
      size_field(bar->width, bar->height);
      if (ImGui::SliderFloat("Fraction", &bar->fraction, 0.0f, 1.0f)) changed();
      color_field("Fill", bar->fill);
      color_field("Background", bar->background);
    } else {
      ImGui::TextUnformatted("Image (runtime only)");
    }
    if (ImGui::Button("Remove")) remove = i;
    ImGui::PopID();
  }
  if (remove >= 0) {
    elements.erase(elements.begin() + remove);
    changed();
  }
}

// State of the Animation Clip panel: the sheet being sliced and the clip being built.
struct ClipPanelState {
  arcane::editor::ClipDraft draft;
  arcane::AssetId sheet = 0;
  char file_name[96] = "new_clip.json";
  char name[64] = "";
  int range_first = 0, range_last = 0;
  float preview_elapsed = 0.0f;
  std::string status;
};

// Authors a clip from a sprite sheet grid: pick the sheet, set the grid, add/reorder
// frames, preview, and save to <assets>/<file>.json (the clip cache hot-reloads it).
// `saved` is called after a successful save so the caller can rescan assets.
void draw_clip_panel(ClipPanelState& st, arcane::editor::TextureCache& textures,
                     const std::vector<arcane::editor::AssetEntry>& images, const std::filesystem::path& assets_dir,
                     float dt, const std::function<void()>& saved) {
  auto& d = st.draft;
  const arcane::editor::AssetEntry* sheet_entry = nullptr;
  for (const auto& e : images) if (e.id == st.sheet) sheet_entry = &e;
  if (ImGui::BeginCombo("Sheet", sheet_entry ? sheet_entry->relative_path.c_str() : "(choose)")) {
    for (const auto& e : images) {
      if (ImGui::Selectable(e.relative_path.c_str(), e.id == st.sheet)) st.sheet = e.id;
    }
    ImGui::EndCombo();
  }
  arcane::Texture* tex = textures.get(st.sheet);
  if (!tex) {
    ImGui::TextUnformatted("Select a sprite sheet image.");
    return;
  }
  ImGui::Text("Sheet %dx%d px", tex->width(), tex->height());
  ImGui::InputInt("Columns", &d.grid.columns);
  ImGui::InputInt("Rows", &d.grid.rows);
  ImGui::InputInt("Frame width (0=auto)", &d.grid.frame_width);
  ImGui::InputInt("Frame height (0=auto)", &d.grid.frame_height);
  ImGui::InputInt("Margin", &d.grid.margin);
  ImGui::InputInt("Spacing", &d.grid.spacing);
  d.grid.columns = std::max(1, d.grid.columns);
  d.grid.rows = std::max(1, d.grid.rows);
  const int cell_count = d.grid.columns * d.grid.rows;

  ImGui::InputInt("From cell", &st.range_first);
  ImGui::InputInt("To cell", &st.range_last);
  st.range_first = std::clamp(st.range_first, 0, cell_count - 1);
  st.range_last = std::clamp(st.range_last, 0, cell_count - 1);
  if (ImGui::Button("Add frames")) d.add_range(st.range_first, st.range_last);
  ImGui::SameLine();
  if (ImGui::Button("Clear")) { d.cells.clear(); d.durations.clear(); }

  ImGui::DragFloat("FPS", &d.fps, 0.1f, 0.5f, 120.0f);
  ImGui::Checkbox("Loop", &d.loop);
  d.durations.resize(d.cells.size(), 0.0f);
  std::optional<std::size_t> remove, up;
  for (std::size_t i = 0; i < d.cells.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    ImGui::SetNextItemWidth(60);
    ImGui::InputInt("##cell", &d.cells[i], 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::DragFloat("##dur", &d.durations[i], 0.005f, 0.0f, 5.0f, d.durations[i] > 0.0f ? "%.3fs" : "default");
    ImGui::SameLine();
    if (ImGui::SmallButton("up") && i > 0) up = i;
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) remove = i;
    ImGui::PopID();
  }
  if (up) d.move_frame(*up, *up - 1);
  if (remove) d.remove_frame(*remove);

  const arcane::AnimationClip clip = arcane::editor::build_clip(d, tex->width(), tex->height());
  if (!clip.frames.empty()) {
    st.preview_elapsed += dt;
    const auto& uv = clip.frames[arcane::select_frame_index(clip, st.preview_elapsed)].uv;
    ImGui::Image(static_cast<ImTextureID>(tex->native_id()), ImVec2(96, 96), ImVec2(uv.u0, uv.v0), ImVec2(uv.u1, uv.v1));
  }

  ImGui::InputText("Clip name", st.name, sizeof(st.name));
  ImGui::InputText("File", st.file_name, sizeof(st.file_name));
  if (ImGui::Button("Save clip")) {
    d.name = st.name;
    const std::filesystem::path rel(st.file_name);
    if (rel.empty() || rel.is_absolute() || rel.string().find("..") != std::string::npos) {
      st.status = "invalid file name";
    } else {
      auto result = arcane::editor::save_clip_file(assets_dir / rel, clip);
      st.status = result ? "saved " + rel.string() : result.error();
      if (result) saved();
    }
  }
  if (!st.status.empty()) ImGui::TextUnformatted(st.status.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = parse_args(argc, argv);
  if (args.project_dir.empty()) {
    ARCANE_LOG_ERROR(kLogCategory, "usage: arcane_editor --project <dir> [--frames N] [--hidden] [--play-frames N]");
    return 1;
  }

  auto window = arcane::Window::create(
      {.width = 1280, .height = 800, .title = "Arcane2D Editor", .hidden = args.hidden});
  if (!window) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create window: {}", window.error());
    return 77;  // no GL-capable display; CTest SKIP convention, see test_renderer_gl.
  }
  if (!arcane::load_gl_functions()) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to load OpenGL functions");
    return 1;
  }

  if (!glad_glGenFramebuffers) {
    ARCANE_LOG_ERROR(kLogCategory, "OpenGL loader state is unavailable in the editor");
    return 1;
  }
#ifdef _WIN32
  if (!glfwGetWin32Window(static_cast<GLFWwindow*>(window->native_handle()))) {
    ARCANE_LOG_ERROR(kLogCategory, "GLFW instance mismatch: window created by a different GLFW copy");
    return 1;
  }
#endif

  auto runtime = arcane::editor::ProjectRuntime::open(args.project_dir, args.module_override);
  if (!runtime) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to open project '{}': {}", args.project_dir, runtime.error());
    return 1;
  }
  arcane::ComponentRegistry& registry = runtime->registry();
  arcane::editor::ProjectSession& session = runtime->session();

  auto renderer = arcane::SpriteRenderer::create();
  if (!renderer) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create sprite renderer: {}", renderer.error());
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  static const std::string ini_path = (std::filesystem::path(args.project_dir) / ".arcane_editor_imgui.ini").string();
  io.IniFilename = ini_path.c_str();

  ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(window->native_handle()), true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  arcane::editor::Selection selection;
  arcane::editor::PlayStopController play_stop;
  arcane::editor::TextureCache textures;
  textures.set_assets(arcane::editor::list_image_assets(session.assets_dir()));

  // Input is only "live" (affects active_input_map()) while Play is
  // running -- the editor's Edit-mode UI handles its own keyboard/mouse
  // input (ImGui) and must not have gameplay actions firing underneath
  // it. See docs/decisions.md.
  arcane::InputActionMap input_map = session.project().input_actions;
  arcane::GlfwInputBackend input_backend(*window);

  arcane::editor::AnimationClipCache clips;
  clips.set_assets(arcane::editor::list_animation_clip_assets(session.assets_dir()));
  arcane::editor::TilesetCache tilesets;
  tilesets.set_assets(arcane::editor::list_animation_clip_assets(session.assets_dir()));
  bool preview_animations = false;
  ClipPanelState clip_panel;
  double asset_poll_accum = 0.0;
  bool preview_particles = false;
  bool paint_mode = false;
  bool terrain_mode = false;
  int paint_terrain = 1;
  arcane::Tileset tileset_draft;
  std::filesystem::path tileset_path;
  std::string tileset_message;
  char tileset_name[128] = "tileset.json";
  int selected_tile = 1;
  int rule_terrain = 1;
  bool paint_rect = false;  // drag a rectangle instead of a freehand brush
  arcane::editor::TilePaintTool paint_tool;
  std::optional<arcane::Vec2> rect_start;  // world position where the rectangle drag began
  bool rect_erase = false;
  auto hud_renderer = arcane::HudRenderer::create();
  if (!hud_renderer) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create HUD renderer: {}", hud_renderer.error());
    return 1;
  }
  arcane::Hud hud;  // only live (and drawn) while Play is running; copied from the project HUD
  arcane::HudInput hud_input;
  arcane::Vec2 viewport_origin{};  // top-left of the Viewport image in window coords (previous frame)
  int viewport_w = 1, viewport_h = 1;

  std::string audio_diagnostic;
  auto audio = arcane::AudioSystem::create_preferred(args.hidden, audio_diagnostic);
  if (!audio_diagnostic.empty()) {
    ARCANE_LOG_WARN(kLogCategory, "audio device unavailable; using null backend: {}", audio_diagnostic);
  }
  if (!audio) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create audio system: {}", audio.error());
    return 1;
  }
  auto begin_play = [&] {
    input_map = session.project().input_actions;
    play_stop.play(session.scene(), registry);
    audio->clear_commands();
    arcane::set_active_audio_system(&*audio);
    hud = session.project().hud;
    hud_input = arcane::HudInput{};
    arcane::set_active_hud(&hud);
    arcane::editor::start_play_on_start_sounds(*play_stop.active_scene(session.scene()), *audio,
                                                arcane::editor::list_audio_assets(session.assets_dir()));
  };
  auto end_play = [&] {
    audio->stop_all();
    play_stop.stop();
    arcane::set_active_input_map(nullptr);
    arcane::set_active_audio_system(nullptr);
    arcane::set_active_hud(nullptr);
  };

  ViewportTarget viewport;
  arcane::FixedTimestep timestep(60.0);
  auto previous_time = std::chrono::steady_clock::now();
  long frame_count = 0;
  long play_tick_count = 0;

  while (!window->should_close()) {
    if (args.frames.has_value() && frame_count >= *args.frames) break;

    window->poll_events();

    if (args.play_frames.has_value() && frame_count == 0) begin_play();

    const auto now = std::chrono::steady_clock::now();
    const double delta_seconds = std::chrono::duration<double>(now - previous_time).count();
    previous_time = now;

    // Hot-reload: twice a second, rescan assets and drop cache entries whose files changed on disk.
    asset_poll_accum += delta_seconds;
    if (asset_poll_accum >= 0.5) {
      asset_poll_accum = 0.0;
      const auto clip_assets = arcane::editor::list_animation_clip_assets(session.assets_dir());
      textures.set_assets(arcane::editor::list_image_assets(session.assets_dir()));
      clips.set_assets(clip_assets);
      tilesets.set_assets(clip_assets);
      textures.refresh();
      clips.refresh();
      tilesets.refresh();
    }
    const int steps = timestep.advance(delta_seconds);
    if (play_stop.mode() == arcane::editor::EditorMode::Play) {
      // HUD first (hit-tested inside the Viewport image); consumed input is hidden from gameplay actions.
      hud_input.update(hud, input_backend, viewport_w, viewport_h, viewport_origin);
      input_map.update(hud_input.gameplay_backend());
      arcane::set_active_input_map(&input_map);
      for (int i = 0; i < steps; ++i) {
        play_stop.tick(static_cast<float>(timestep.fixed_delta_seconds()));
        ++play_tick_count;
      }
      if (args.play_frames.has_value() && play_tick_count >= *args.play_frames) {
        end_play();
      }
    } else if (preview_animations) {
      arcane::editor::preview_animations(session.scene(), static_cast<float>(delta_seconds));
    }
    if (preview_particles && play_stop.mode() != arcane::editor::EditorMode::Play) {
      arcane::editor::preview_particles(session.scene(), static_cast<float>(delta_seconds));
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save")) {
          auto saved = session.save(registry);
          if (!saved) ARCANE_LOG_ERROR(kLogCategory, "save failed: {}", saved.error());
        }
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    if (ImGui::Begin("Toolbar")) {
      const bool playing = play_stop.mode() == arcane::editor::EditorMode::Play;
      if (!playing) {
        if (ImGui::Button("Play")) begin_play();
      } else {
        if (ImGui::Button("Stop")) end_play();
      }
      ImGui::SameLine();
      ImGui::Checkbox("Preview animations", &preview_animations);
      ImGui::SameLine();
      if (ImGui::Checkbox("Preview particles", &preview_particles)) {
        arcane::editor::restart_particles(session.scene());  // clears stale particles on off, fresh start on on
      }
      ImGui::SameLine();
      ImGui::TextUnformatted(playing ? "Mode: Play" : "Mode: Edit");
      ImGui::SameLine();
      ImGui::TextUnformatted(session.dirty() ? "*modified*" : "");
    }
    ImGui::End();

    arcane::Scene* active_scene = play_stop.active_scene(session.scene());
    const bool editable = play_stop.mode() == arcane::editor::EditorMode::Edit;

    if (ImGui::Begin("Hierarchy")) {
      if (editable && ImGui::Button("New Node")) {
        arcane::Node* parent = nullptr;
        if (selection.has_selection()) parent = active_scene->find_node(*selection.selected());
        arcane::Node* created = active_scene->create_node("Node", parent);
        selection.select(created->id());
        session.mark_dirty();
      }
      ImGui::SameLine();
      if (editable && selection.has_selection() && ImGui::Button("Delete")) {
        active_scene->destroy_node(*selection.selected());
        active_scene->flush_destroyed();
        selection.clear();
        session.mark_dirty();
      }

      std::function<void(arcane::Node*)> draw_node = [&](arcane::Node* node) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selection.selected() == node->id()) flags |= ImGuiTreeNodeFlags_Selected;
        if (node->children().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        const bool open = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(node->id())), flags, "%s", node->name().c_str());
        if (ImGui::IsItemClicked()) selection.select(node->id());

        if (editable && ImGui::BeginDragDropSource()) {
          arcane::NodeId id = node->id();
          ImGui::SetDragDropPayload("ARCANE_NODE", &id, sizeof(id));
          ImGui::TextUnformatted(node->name().c_str());
          ImGui::EndDragDropSource();
        }
        if (editable && ImGui::BeginDragDropTarget()) {
          if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ARCANE_NODE")) {
            arcane::NodeId dragged = *static_cast<const arcane::NodeId*>(payload->Data);
            arcane::Node* dragged_node = active_scene->find_node(dragged);
            if (dragged_node && active_scene->reparent(dragged_node, node)) session.mark_dirty();
          }
          ImGui::EndDragDropTarget();
        }

        if (open) {
          for (const auto& child : node->children()) draw_node(child.get());
          ImGui::TreePop();
        }
      };
      for (const auto& root : active_scene->roots()) draw_node(root.get());
    }
    ImGui::End();

    if (ImGui::Begin("Inspector")) {
      arcane::Node* selected_node =
          selection.has_selection() ? active_scene->find_node(*selection.selected()) : nullptr;
      if (!selected_node) {
        ImGui::TextUnformatted("No selection");
      } else {
        ImGui::BeginDisabled(!editable);
        char name_buf[128];
        std::snprintf(name_buf, sizeof(name_buf), "%s", selected_node->name().c_str());
        if (ImGui::InputText("Name", name_buf, sizeof(name_buf))) {
          selected_node->set_name(name_buf);
          session.mark_dirty();
        }
        auto& transform = selected_node->local_transform();
        float pos[2] = {transform.position.x, transform.position.y};
        if (ImGui::DragFloat2("Position", pos, 0.5f)) {
          transform.position = {pos[0], pos[1]};
          session.mark_dirty();
        }
        float rotation_degrees = transform.rotation_radians * 180.0f / 3.14159265f;
        if (ImGui::DragFloat("Rotation", &rotation_degrees, 1.0f)) {
          transform.rotation_radians = rotation_degrees * 3.14159265f / 180.0f;
          session.mark_dirty();
        }
        float scale[2] = {transform.scale.x, transform.scale.y};
        if (ImGui::DragFloat2("Scale", scale, 0.05f)) {
          transform.scale = {scale[0], scale[1]};
          session.mark_dirty();
        }

        for (const auto& component : selected_node->components()) {
          ImGui::Separator();
          ImGui::TextUnformatted(component->type_name().c_str());
          if (const auto* descriptors = registry.properties_of(component->type_name())) {
            for (const auto& descriptor : *descriptors) {
              ImGui::PushID(descriptor.name.c_str());
              if (draw_property_editor(descriptor, *component)) session.mark_dirty();
              ImGui::PopID();
            }
          }
        }

        if (editable) {
          ImGui::Separator();
          if (ImGui::BeginCombo("Add Component", "...")) {
            for (const auto& type_name : registry.type_names()) {
              if (selected_node->find_component(type_name)) continue;
              if (ImGui::Selectable(type_name.c_str())) {
                active_scene->add_component(selected_node, type_name, registry);
                session.mark_dirty();
              }
            }
            ImGui::EndCombo();
          }
        }
        ImGui::EndDisabled();
      }
    }
    ImGui::End();

    if (ImGui::Begin("Assets")) {
      auto assets = arcane::editor::list_image_assets(session.assets_dir());
      for (const auto& asset : assets) {
        const bool pressed = ImGui::Selectable(asset.relative_path.c_str());
        if (pressed && editable && selection.has_selection()) {
          arcane::Node* selected_node = active_scene->find_node(*selection.selected());
          auto* sprite = selected_node ? static_cast<arcane::Sprite*>(selected_node->find_component("Sprite"))
                                        : nullptr;
          auto* emitter = selected_node ? static_cast<arcane::ParticleEmitter*>(selected_node->find_component("ParticleEmitter"))
                                        : nullptr;
          if (sprite) {
            sprite->texture = asset.id;
            session.mark_dirty();
          } else if (emitter) {
            emitter->texture = asset.id;
            session.mark_dirty();
          }
        }
      }

      // Clips and sounds: clicking one assigns it to the selected node's
      // AnimatedSprite / AudioSource (the Inspector then shows the id, and
      // "Preview animations" in the Toolbar plays it in the Viewport).
      auto assign_asset = [&](const char* header, const std::vector<arcane::editor::AssetEntry>& list,
                               const char* component_name, auto set_id) {
        if (!ImGui::CollapsingHeader(header)) return;
        for (const auto& asset : list) {
          if (!ImGui::Selectable(asset.relative_path.c_str()) || !editable || !selection.has_selection()) continue;
          arcane::Node* selected_node = active_scene->find_node(*selection.selected());
          if (auto* component = selected_node ? selected_node->find_component(component_name) : nullptr) {
            set_id(component, asset.id);
            session.mark_dirty();
          }
        }
      };
      assign_asset("Animation clips", arcane::editor::list_animation_clip_assets(session.assets_dir()),
                   "AnimatedSprite", [](arcane::Component* c, arcane::AssetId id) {
                     auto* animated = static_cast<arcane::AnimatedSprite*>(c);
                     animated->clip = id;
                     animated->reset();
                   });
      assign_asset("Tilesets", arcane::editor::list_animation_clip_assets(session.assets_dir()), "Tilemap",
                   [](arcane::Component* c, arcane::AssetId id) { static_cast<arcane::Tilemap*>(c)->tileset = id; });
      assign_asset("Sounds", arcane::editor::list_audio_assets(session.assets_dir()), "AudioSource",
                   [](arcane::Component* c, arcane::AssetId id) { static_cast<arcane::AudioSource*>(c)->clip = id; });
    }
    ImGui::End();

    arcane::Node* paint_node = (editable && selection.has_selection()) ? active_scene->find_node(*selection.selected())
                                                                         : nullptr;
    auto* paint_map = paint_node ? static_cast<arcane::Tilemap*>(paint_node->find_component("Tilemap")) : nullptr;
    if (ImGui::Begin("Tilemap")) {
      if (!paint_map) {
        ImGui::TextUnformatted("Select a node with a Tilemap component (Edit mode).");
      } else {
        ImGui::Checkbox("Paint in viewport (left = paint, right = erase)", &paint_mode);
        ImGui::Checkbox("Terrain brush", &terrain_mode);
        if (terrain_mode) { ImGui::InputInt("Terrain id", &paint_terrain); paint_terrain = std::max(1, paint_terrain); }
        else ImGui::Checkbox("Fill rectangle (drag)", &paint_rect);
        if (ImGui::Button("Undo") && paint_tool.undo(*paint_map)) session.mark_dirty();
        ImGui::SameLine();
        if (ImGui::Button("Redo") && paint_tool.redo(*paint_map)) session.mark_dirty();
        if (paint_map->layer_count() == 0 || ImGui::Button("Add layer")) {
          paint_map->add_layer();
          session.mark_dirty();
        }
        ImGui::InputInt("Layer", &paint_tool.layer);
        paint_tool.layer = std::clamp(paint_tool.layer, 0, std::max(0, paint_map->layer_count() - 1));
        const arcane::Tileset* tileset = tilesets.get(paint_map->tileset);
        if (!tileset) {
          ImGui::TextUnformatted("Assign a tileset from Assets > Tilesets.");
        } else {
          if (ImGui::Selectable("0: erase", paint_tool.tile == 0)) paint_tool.tile = 0;
          for (int id = 1; id <= tileset->tile_count; ++id) {
            const std::string label = std::to_string(id);
            if (ImGui::Selectable(label.c_str(), paint_tool.tile == id, 0, ImVec2(28, 0))) paint_tool.tile = id;
            if (id % 8 != 0 && id < tileset->tile_count) ImGui::SameLine();
          }
        }
      }
    }
    ImGui::End();

    if (ImGui::Begin("Tileset")) {
      ImGui::BeginDisabled(!editable);
      ImGui::InputText("New filename", tileset_name, sizeof(tileset_name));
      if (ImGui::Button("New tileset")) {
        const std::filesystem::path name(tileset_name);
        if (!name.empty() && name == name.filename() && name.extension() == ".json") {
          tileset_path = session.assets_dir() / name; tileset_draft = {}; selected_tile = 1;
          tileset_message = "New tileset; save to create it.";
        } else tileset_message = "Use a simple .json filename.";
      }
      if (ImGui::TreeNode("Open tileset")) {
        for (const auto& asset : arcane::editor::list_animation_clip_assets(session.assets_dir())) {
          if (ImGui::Selectable(asset.relative_path.c_str())) {
            std::ifstream in(asset.absolute_path);
            std::string content((std::istreambuf_iterator<char>(in)), {});
            auto loaded = arcane::load_tileset_from_json(content);
            if (loaded) { tileset_draft = *loaded; tileset_path = asset.absolute_path; selected_tile = 1; tileset_message.clear(); }
            else tileset_message = loaded.error();
          }
        }
        ImGui::TreePop();
      }
      if (!tileset_path.empty()) {
        ImGui::Text("File: %s", tileset_path.filename().string().c_str());
        if (ImGui::BeginCombo("Texture", std::to_string(tileset_draft.texture).c_str())) {
          for (const auto& asset : arcane::editor::list_image_assets(session.assets_dir()))
            if (ImGui::Selectable(asset.relative_path.c_str(), asset.id == tileset_draft.texture)) tileset_draft.texture = asset.id;
          ImGui::EndCombo();
        }
        ImGui::InputInt("Tile width (pixels)", &tileset_draft.tile_width);
        ImGui::InputInt("Tile height (pixels)", &tileset_draft.tile_height);
        ImGui::InputInt("Columns", &tileset_draft.columns);
        ImGui::InputInt("Tile count", &tileset_draft.tile_count);
        tileset_draft.tile_width = std::max(1, tileset_draft.tile_width);
        tileset_draft.tile_height = std::max(1, tileset_draft.tile_height);
        tileset_draft.columns = std::max(1, tileset_draft.columns);
        tileset_draft.tile_count = std::clamp(tileset_draft.tile_count, 1, 4096);
        ImGui::InputInt("Tile id", &selected_tile);
        selected_tile = std::clamp(selected_tile, 1, tileset_draft.tile_count);
        tileset_draft.tiles.resize(static_cast<std::size_t>(tileset_draft.tile_count));
        auto& info = tileset_draft.tiles[static_cast<std::size_t>(selected_tile - 1)];
        ImGui::Checkbox("Solid", &info.solid);
        ImGui::InputInt("Tile terrain (0 = none)", &info.terrain);
        info.terrain = std::max(0, info.terrain);
        ImGui::InputInt("Rule terrain", &rule_terrain); rule_terrain = std::max(1, rule_terrain);
        auto rule = std::find_if(tileset_draft.terrains.begin(), tileset_draft.terrains.end(), [&](const auto& r) { return r.terrain == rule_terrain; });
        if (rule == tileset_draft.terrains.end() && ImGui::Button("Add terrain rule")) {
          tileset_draft.terrains.push_back({}); tileset_draft.terrains.back().terrain = rule_terrain;
          rule = std::prev(tileset_draft.terrains.end());
        }
        if (rule != tileset_draft.terrains.end() && ImGui::TreeNode("Mask rules (N=1 E=2 S=4 W=8)")) {
          for (int mask = 0; mask < 16; ++mask) {
            ImGui::PushID(mask); ImGui::InputInt(std::to_string(mask).c_str(), &rule->tiles[static_cast<std::size_t>(mask)]); ImGui::PopID();
          }
          ImGui::TreePop();
        }
        if (ImGui::Button("Save tileset")) {
          std::ofstream out(tileset_path, std::ios::binary | std::ios::trunc);
          out << arcane::save_tileset_to_json(tileset_draft); out.close();
          tileset_message = out ? "Saved." : "Could not save tileset.";
          tilesets.set_assets(arcane::editor::list_animation_clip_assets(session.assets_dir()));
          tilesets.refresh();
        }
      }
      ImGui::TextWrapped("%s", tileset_message.c_str());
      ImGui::EndDisabled();
    }
    ImGui::End();

    if (ImGui::Begin("Animation Clip")) {
      ImGui::BeginDisabled(!editable);
      draw_clip_panel(clip_panel, textures, arcane::editor::list_image_assets(session.assets_dir()),
                      session.assets_dir(), static_cast<float>(delta_seconds), [&] {
                        clips.set_assets(arcane::editor::list_animation_clip_assets(session.assets_dir()));
                      });
      ImGui::EndDisabled();
    }
    ImGui::End();

    if (ImGui::Begin("Input Map")) {
      ImGui::BeginDisabled(!editable);
      auto& map = session.mutable_project().input_actions;
      static char action_name[128] = {};
      ImGui::InputText("New action", action_name, sizeof(action_name));
      ImGui::SameLine();
      if (ImGui::Button("Add action") && action_name[0] != '\0') {
        bool exists = false;
        for (const auto& action : map.actions()) exists |= action.name == action_name;
        if (!exists) { map.set_action({action_name, {}}); session.mark_dirty(); }
        action_name[0] = '\0';
      }
      // Work on copies because set_action and removal may invalidate the map's storage.
      const auto actions = map.actions();
      for (auto action : actions) {
        ImGui::PushID(action.name.c_str());
        if (ImGui::TreeNode(action.name.c_str())) {
          bool changed = false;
          changed |= ImGui::SliderInt("Player (-1 = any)", &action.player, -1, arcane::kMaxGamepads - 1);
          for (std::size_t i = 0; i < action.bindings.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            auto& binding = action.bindings[i];
            int device = static_cast<int>(binding.device);
            const char* devices[] = {"Keyboard", "Mouse button", "Gamepad button", "Gamepad axis"};
            if (ImGui::Combo("Device", &device, devices, 4)) {
              binding.device = static_cast<arcane::InputDevice>(device); changed = true;
            }
            changed |= ImGui::InputInt("Code", &binding.code);
            if (device >= 2) changed |= ImGui::SliderInt("Pad (-1 = player)", &binding.pad, -1, arcane::kMaxGamepads - 1);
            if (device == 3) {
              changed |= ImGui::SliderInt("Direction (-1 / 0 / 1)", &binding.axis_sign, -1, 1);
              changed |= ImGui::SliderFloat("Deadzone", &binding.deadzone, 0.0f, 0.99f);
              changed |= ImGui::SliderFloat("Button threshold", &binding.threshold, 0.0f, 1.0f);
            }
            if (ImGui::Button("Remove binding")) {
              action.bindings.erase(action.bindings.begin() + static_cast<std::ptrdiff_t>(i));
              changed = true; ImGui::PopID(); break;
            }
            ImGui::Separator();
            ImGui::PopID();
          }
          if (ImGui::Button("Add binding")) { action.bindings.push_back({}); changed = true; }
          ImGui::SameLine();
          const bool remove = ImGui::Button("Remove action");
          if (remove) { map.remove_action(action.name); session.mark_dirty(); }
          else if (changed) { map.set_action(std::move(action)); session.mark_dirty(); }
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      ImGui::EndDisabled();
    }
    ImGui::End();

    if (ImGui::Begin("HUD")) {
      ImGui::BeginDisabled(!editable);
      draw_hud_editor(session.mutable_project().hud, [&] { session.mark_dirty(); });
      ImGui::EndDisabled();
    }
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Viewport")) {
      ImVec2 size = ImGui::GetContentRegionAvail();
      const int vw = std::max(1, static_cast<int>(size.x));
      const int vh = std::max(1, static_cast<int>(size.y));
      viewport.ensure_size(vw, vh);

      viewport.bind();
      arcane::editor::render_scene(*active_scene, *renderer, textures, viewport.width(), viewport.height(), &clips,
                                  editable ? nullptr : &*hud_renderer, editable ? nullptr : &hud,
                                  &tilesets, !editable);
      ViewportTarget::unbind();

      ImGui::Image(static_cast<ImTextureID>(static_cast<std::uintptr_t>(viewport.texture())),
                    ImVec2(static_cast<float>(viewport.width()), static_cast<float>(viewport.height())),
                    ImVec2(0, 1), ImVec2(1, 0));
      const ImVec2 image_min = ImGui::GetItemRectMin();
      viewport_origin = {image_min.x, image_min.y};
      viewport_w = viewport.width();
      viewport_h = viewport.height();

      if (paint_mode && paint_map) {
        const bool left = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool right = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const ImVec2 origin = ImGui::GetItemRectMin();
        const ImVec2 mouse = ImGui::GetMousePos();
        const arcane::Vec2 world =
            arcane::editor::viewport_to_world(*active_scene, mouse.x - origin.x, mouse.y - origin.y, viewport.width(),
                                              viewport.height());
        const auto node_world = paint_node->world_transform();
        if (ImGui::IsItemHovered() && (left || right)) {
          if (paint_rect && !terrain_mode) {
            if (!rect_start) {
              rect_start = world;
              rect_erase = right;
            }
          } else {
            paint_tool.begin_stroke();
            const auto* brush_set = terrain_mode ? tilesets.get(paint_map->tileset) : nullptr;
            if (terrain_mode ? (brush_set && paint_tool.terrain_brush(*paint_map, *brush_set, node_world, world, paint_terrain, right))
                             : paint_tool.brush(*paint_map, node_world, world, right)) session.mark_dirty();
          }
        } else if (!left && !right) {
          if (rect_start) {
            paint_tool.begin_stroke();
            if (paint_tool.fill_rect(*paint_map, node_world, *rect_start, world, rect_erase)) session.mark_dirty();
            rect_start.reset();
          }
          paint_tool.end_stroke();
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !io.KeyShift) {
          if (paint_tool.undo(*paint_map)) session.mark_dirty();
        } else if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)))) {
          if (paint_tool.redo(*paint_map)) session.mark_dirty();
        }
      }
    }
    ImGui::End();

    ImGui::Render();

    int fb_width = 0, fb_height = 0;
    window->framebuffer_size(fb_width, fb_height);
    glViewport(0, 0, fb_width, fb_height);
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    window->swap_buffers();
    ++frame_count;
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  ARCANE_LOG_INFO(kLogCategory, "exiting after {} frames", frame_count);
  return 0;
}
