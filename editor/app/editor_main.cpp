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
#include <map>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <functional>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "arcane_editor/clip_authoring.hpp"

#include "arcane/audio.hpp"
#include "arcane/game_context.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/debug.hpp"
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
#include "arcane_tooling/command.hpp"
#include "arcane_tooling/edit_session.hpp"
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

// Draws one property widget; returns the edited value (the caller applies it through a command).
std::optional<arcane::PropertyValue> draw_property_editor(const arcane::PropertyDescriptor& descriptor,
                                                         arcane::Component& component) {
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
  if (changed) return value;
  return std::nullopt;
}

// Runs a registry command as an editor edit; the key coalesces a drag into one undo step ("" = never merges).
using EditFn = std::function<void(const arcane::tooling::Invocation&, const std::string& coalesce_key)>;

// Inspector-style editor for the project's authored HUD. The HUD is read-only here: every widget edit is a
// hud.* command (the same ones as the CLI) run through `edit`, so it is undoable and keeps dirty state.
void draw_hud_editor(const arcane::Hud& hud, const std::vector<arcane::editor::AssetEntry>& images, const EditFn& edit) {
  using arcane::tooling::Json;
  static constexpr const char* kAnchors[] = {"TopLeft", "Top", "TopRight", "Left", "Center",
                                             "Right", "BottomLeft", "Bottom", "BottomRight"};
  int index = 0;  // element being drawn
  auto set = [&](const char* field, Json value) {
    edit({"hud.set", {{"index", index}, {"field", field}, {"value", std::move(value)}}},
         "hud.set:" + std::to_string(index) + ":" + field);
  };
  auto text_field = [&](const char* label, const char* field, const std::string& current) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", current.c_str());
    if (ImGui::InputText(label, buf, sizeof(buf))) set(field, std::string(buf));
  };
  auto layout = [&](arcane::HudAnchor anchor, float ox, float oy) {
    int a = static_cast<int>(anchor);
    if (ImGui::Combo("Anchor", &a, kAnchors, 9)) set("anchor", kAnchors[a]);
    float off[2] = {ox, oy};
    if (ImGui::DragFloat2("Offset", off, 0.5f)) {
      if (off[0] != ox) set("offset_x", off[0]);  // only the dragged axis, so one drag stays one undo step
      if (off[1] != oy) set("offset_y", off[1]);
    }
  };
  auto size_field = [&](float w, float h) {
    float v[2] = {w, h};
    if (ImGui::DragFloat2("Size", v, 0.5f, 1.0f, 4000.0f)) {
      if (v[0] != w) set("width", v[0]);
      if (v[1] != h) set("height", v[1]);
    }
  };
  auto color_field = [&](const char* label, const char* field, const arcane::Color& c) {
    float v[4] = {c.r, c.g, c.b, c.a};
    if (ImGui::ColorEdit4(label, v)) set(field, Json::array({v[0], v[1], v[2], v[3]}));
  };
  auto font_scale_field = [&](float current) {
    float v = current;
    if (ImGui::DragFloat("Font scale", &v, 0.1f, 0.5f, 16.0f)) set("font_scale", v);
  };
  auto add = [&](const char* type) { edit({"hud.add", {{"type", type}}}, {}); };

  if (ImGui::Button("+ Button")) add("button");
  ImGui::SameLine();
  if (ImGui::Button("+ Text")) add("text");
  ImGui::SameLine();
  if (ImGui::Button("+ Bar")) add("bar");
  ImGui::SameLine();
  if (ImGui::Button("+ Image")) add("image");

  const auto& elements = hud.elements();
  int remove = -1;
  for (index = 0; index < static_cast<int>(elements.size()); ++index) {
    ImGui::PushID(index);
    ImGui::Separator();
    if (const auto* b = std::get_if<arcane::HudButton>(&elements[index])) {
      ImGui::Text("Button");
      text_field("Id", "id", b->id);
      text_field("Label", "label", b->label);
      layout(b->anchor, b->offset_x, b->offset_y);
      size_field(b->width, b->height);
      font_scale_field(b->font_scale);
      bool enabled = b->enabled;
      if (ImGui::Checkbox("Enabled", &enabled)) set("enabled", enabled);
      color_field("Normal", "normal", b->normal);
      color_field("Hover", "hover", b->hover);
      color_field("Pressed", "pressed", b->pressed);
      color_field("Disabled", "disabled", b->disabled);
      color_field("Text color", "text_color", b->text_color);
    } else if (const auto* t = std::get_if<arcane::HudText>(&elements[index])) {
      ImGui::Text("Text");
      text_field("Id", "id", t->id);
      text_field("Text", "text", t->text);
      layout(t->anchor, t->offset_x, t->offset_y);
      font_scale_field(t->font_scale);
      color_field("Color", "color", t->color);
    } else if (const auto* bar = std::get_if<arcane::HudProgressBar>(&elements[index])) {
      ImGui::Text("Progress bar");
      text_field("Id", "id", bar->id);
      layout(bar->anchor, bar->offset_x, bar->offset_y);
      size_field(bar->width, bar->height);
      float fraction = bar->fraction;
      if (ImGui::SliderFloat("Fraction", &fraction, 0.0f, 1.0f)) set("fraction", fraction);
      color_field("Fill", "fill", bar->fill);
      color_field("Background", "background", bar->background);
    } else if (const auto* image = std::get_if<arcane::HudImage>(&elements[index])) {
      ImGui::Text("Image");
      text_field("Id", "id", image->id);
      const arcane::editor::AssetEntry* current = nullptr;
      for (const auto& e : images) if (e.id == image->texture_asset) current = &e;
      if (ImGui::BeginCombo("Texture", current ? current->relative_path.c_str() : "(none)")) {
        if (ImGui::Selectable("(none)", image->texture_asset == 0)) set("texture_asset", std::uint64_t{0});
        for (const auto& e : images) {
          if (ImGui::Selectable(e.relative_path.c_str(), e.id == image->texture_asset)) {
            set("texture_asset", static_cast<std::uint64_t>(e.id));
          }
        }
        ImGui::EndCombo();
      }
      float uv[4] = {image->uv.u0, image->uv.v0, image->uv.u1, image->uv.v1};
      if (ImGui::DragFloat4("UV (u0 v0 u1 v1)", uv, 0.005f)) set("uv", Json::array({uv[0], uv[1], uv[2], uv[3]}));
      layout(image->anchor, image->offset_x, image->offset_y);
      size_field(image->width, image->height);
      color_field("Tint", "tint", image->tint);
    }
    if (ImGui::Button("Remove")) remove = index;
    ImGui::PopID();
  }
  if (remove >= 0) edit({"hud.remove", {{"index", remove}}}, {});
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
// frames, preview, and save to <assets>/<file>.json (the clip cache hot-reloads it). The draft is transient view state;
// Open clip reads with clip.info and Save runs clip.create (overwrite, one undoable step) through `run_clip`, which
// returns the command result or an error message.
// `saved` is called after a successful save so the caller can rescan assets.
void draw_clip_panel(ClipPanelState& st, arcane::editor::TextureCache& textures,
                     const std::vector<arcane::editor::AssetEntry>& images, const std::filesystem::path& project_dir,
                     const std::filesystem::path& assets_dir, float dt,
                     const std::function<std::expected<arcane::tooling::CommandResult, std::string>(const arcane::tooling::Invocation&)>& run_clip,
                     const std::function<void()>& saved) {
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
  // Reopen: load the clip named in File into the draft using the grid set above (rejects frames off the grid).
  if (ImGui::Button("Open clip")) {
    const std::filesystem::path rel(st.file_name);
    if (rel.empty() || rel.is_absolute() || rel.string().find("..") != std::string::npos) {
      st.status = "invalid file name";
    } else if (!sheet_entry) {
      st.status = "the sheet is not in the asset list";
    } else {
      // clip.info with the sheet and grid reopens the clip on that grid (frames off the grid are an error).
      const std::string clip_path = (std::filesystem::relative(assets_dir, project_dir) / rel).generic_string();
      const std::string sheet_path = std::filesystem::relative(sheet_entry->absolute_path, project_dir).generic_string();
      auto opened = run_clip({"clip.info",
                              {{"clip", clip_path}, {"sheet", sheet_path}, {"columns", d.grid.columns}, {"rows", d.grid.rows},
                               {"frame_width", d.grid.frame_width}, {"frame_height", d.grid.frame_height},
                               {"margin", d.grid.margin}, {"spacing", d.grid.spacing}}});
      if (!opened) {
        st.status = "cannot reopen on this grid: " + opened.error();
      } else {
        const auto& v = opened->value;
        d.name = v["name"].get<std::string>();
        d.loop = v["loop"].get<bool>();
        d.fps = v["fps"].get<float>();
        d.cells = v["cells"].get<std::vector<int>>();
        d.durations = v["durations"].get<std::vector<float>>();
        std::snprintf(st.name, sizeof(st.name), "%s", d.name.c_str());
        st.status = "opened " + rel.string();
      }
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Save clip")) {
    d.name = st.name;
    const std::filesystem::path rel(st.file_name);
    if (rel.empty() || rel.is_absolute() || rel.string().find("..") != std::string::npos) {
      st.status = "invalid file name";
    } else if (!sheet_entry) {
      st.status = "the sheet is not in the asset list";
    } else {
      // The same clip.create the CLI runs: project-relative sheet and clip paths, the draft's grid, cells and timing.
      const std::string clip_path = (std::filesystem::relative(assets_dir, project_dir) / rel).generic_string();
      const std::string sheet_path = std::filesystem::relative(sheet_entry->absolute_path, project_dir).generic_string();
      const arcane::tooling::Invocation call{
          "clip.create",
          {{"clip", clip_path}, {"sheet", sheet_path}, {"columns", d.grid.columns}, {"rows", d.grid.rows},
           {"frame_width", d.grid.frame_width}, {"frame_height", d.grid.frame_height}, {"margin", d.grid.margin},
           {"spacing", d.grid.spacing}, {"cells", d.cells}, {"fps", d.fps}, {"durations", d.durations},
           {"loop", d.loop}, {"name", d.name}, {"overwrite", true}}};
      const auto done = run_clip(call);
      st.status = done ? "saved " + rel.string() : done.error();
      if (done) saved();
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

  // The editor opens through the same context the `arcane` CLI uses; scene edits are registry commands.
  // Compatible policy keeps legacy sample projects openable (Save still refuses to drop unknown data).
  auto context = arcane::tooling::ProjectContext::open(args.project_dir, args.module_override, {},
                                                      arcane::editor::SceneLoadPolicy::Compatible);
  if (!context) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to open project '{}': {}", args.project_dir, context.error().message);
    return 1;
  }
  arcane::ComponentRegistry& registry = (*context)->registry();
  arcane::editor::ProjectSession& session = (*context)->session();
  arcane::tooling::CommandRegistry commands;
  arcane::tooling::register_all_commands(commands);
  arcane::tooling::EditSession edits(**context, commands);
  // Runs a scene command; failures are logged and leave the scene unchanged.
  auto run_edit = [&](const arcane::tooling::Invocation& call,
                      const std::string& coalesce_key = {}) -> std::optional<arcane::tooling::CommandResult> {
    auto done = edits.run(call, coalesce_key);
    if (!done) {
      ARCANE_LOG_ERROR(kLogCategory, "{} failed: {}", call.command, done.error().message);
      return std::nullopt;
    }
    return *done;
  };
  auto node_selector = [](arcane::NodeId id) { return "id:" + std::to_string(id); };

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
  // Paint tool settings; every cell edit is a tilemap.* command in the edit history (one drag = one undo step).
  int paint_layer = 0;
  int paint_tile = 1;
  bool tilemap_layer_failed = false;  // stop auto-adding the first layer if the command keeps failing
  bool paint_stroke = false;  // a viewport drag is in progress: keep coalescing into one undo step
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
  arcane::GameContext game_context(session.project_dir(), registry);
  auto begin_play = [&] {
    game_context.take_restart_request();
    arcane::set_active_game_context(&game_context);
    input_map = session.project().input_actions;
    play_stop.play(session.scene(), registry);
    audio->clear_commands();
    arcane::set_active_audio_system(&*audio);
    hud = session.project().hud;
    for (auto& element : hud.elements()) {
      if (auto* image = std::get_if<arcane::HudImage>(&element)) {
        // Live copy only (never saved): resolve the persisted asset id; 0 or an unknown id draws nothing.
        arcane::HudImage& live = *image;
        live.texture = live.texture_asset != 0 ? textures.get(live.texture_asset) : nullptr;
      }
    }
    hud_input = arcane::HudInput{};
    arcane::set_active_hud(&hud);
    arcane::editor::start_play_on_start_sounds(*play_stop.active_scene(session.scene()), *audio,
                                                arcane::editor::list_audio_assets(session.assets_dir()));
  };
  auto end_play = [&] {
    audio->stop_all();
    play_stop.stop();
    arcane::set_active_input_map(nullptr);
    arcane::set_active_game_context(nullptr);
    arcane::set_active_audio_system(nullptr);
    arcane::set_active_hud(nullptr);
  };

  ViewportTarget viewport;
  arcane::FixedTimestep timestep(60.0);
  auto previous_time = std::chrono::steady_clock::now();
  long frame_count = 0;
  long play_tick_count = 0;
  bool overlay_visible = session.project().debug.overlay_visible;
  arcane::FrameTimer frame_timer;

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
        if (game_context.take_restart_request()) {  // gameplay asked for a restart: replay from the authored scene
          play_stop.stop();
          play_stop.play(session.scene(), registry);
        }
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

    // Scene undo/redo (commands, tile strokes included); not while typing text.
    bool do_undo = false, do_redo = false;
    if (!ImGui::GetIO().WantTextInput) {
      const ImGuiIO& keys = ImGui::GetIO();
      if (keys.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !keys.KeyShift) do_undo = true;
      if (keys.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (keys.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)))) {
        do_redo = true;
      }
    }
    // A drag ends when no widget is active any more: the next edit starts a new undo step.
    if (!ImGui::IsAnyItemActive() && !paint_stroke) edits.end_gesture();

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save")) {
          auto saved = session.save(registry);
          if (!saved) ARCANE_LOG_ERROR(kLogCategory, "save failed: {}", saved.error());
          else edits.mark_saved();
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Edit")) {
        const bool can_edit = play_stop.mode() == arcane::editor::EditorMode::Edit;
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, can_edit && edits.can_undo())) do_undo = true;
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, can_edit && edits.can_redo())) do_redo = true;
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    if (play_stop.mode() == arcane::editor::EditorMode::Edit) {
      if (do_undo && edits.can_undo()) {
        if (auto done = edits.undo(); !done) ARCANE_LOG_ERROR(kLogCategory, "undo failed: {}", done.error().message);
      } else if (do_redo && edits.can_redo()) {
        if (auto done = edits.redo(); !done) ARCANE_LOG_ERROR(kLogCategory, "redo failed: {}", done.error().message);
      }
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
      ImGui::TextUnformatted(session.dirty() || edits.modified() ? "*modified*" : "");
    }
    ImGui::End();

    arcane::Scene* active_scene = play_stop.active_scene(session.scene());
    const bool editable = play_stop.mode() == arcane::editor::EditorMode::Edit;

    if (ImGui::Begin("Hierarchy")) {
      if (editable && ImGui::Button("New Node")) {
        arcane::tooling::Json add_args = {{"name", "Node"}};
        if (selection.has_selection() && active_scene->find_node(*selection.selected())) {
          add_args["parent"] = node_selector(*selection.selected());
        }
        if (auto created = run_edit({"node.add", add_args})) selection.select((*created).value["id"].get<arcane::NodeId>());
      }
      ImGui::SameLine();
      if (editable && selection.has_selection() && ImGui::Button("Delete")) {
        if (run_edit({"node.remove", {{"node", node_selector(*selection.selected())}}})) selection.clear();
      }

      std::optional<std::pair<arcane::NodeId, arcane::NodeId>> pending_reparent;
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
            pending_reparent = {dragged, node->id()};  // applied after the tree is drawn
          }
          ImGui::EndDragDropTarget();
        }

        if (open) {
          for (const auto& child : node->children()) draw_node(child.get());
          ImGui::TreePop();
        }
      };
      for (const auto& root : active_scene->roots()) draw_node(root.get());
      if (pending_reparent) {
        run_edit({"node.reparent", {{"node", node_selector(pending_reparent->first)},
                                   {"parent", node_selector(pending_reparent->second)}}});
      }
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
          // Typing coalesces into one undo step; an invalid name (e.g. with '/') is refused and logged.
          auto renamed = edits.run({"node.rename", {{"node", node_selector(selected_node->id())}, {"name", name_buf}}},
                                   "rename:" + std::to_string(selected_node->id()));
          if (!renamed) ARCANE_LOG_ERROR(kLogCategory, "rename failed: {}", renamed.error().message);
        }
        arcane::Transform2D transform = selected_node->local_transform();
        bool transform_changed = false;
        float pos[2] = {transform.position.x, transform.position.y};
        if (ImGui::DragFloat2("Position", pos, 0.5f)) {
          transform.position = {pos[0], pos[1]};
          transform_changed = true;
        }
        float rotation_degrees = transform.rotation_radians * 180.0f / 3.14159265f;
        if (ImGui::DragFloat("Rotation", &rotation_degrees, 1.0f)) {
          transform.rotation_radians = rotation_degrees * 3.14159265f / 180.0f;
          transform_changed = true;
        }
        float scale[2] = {transform.scale.x, transform.scale.y};
        if (ImGui::DragFloat2("Scale", scale, 0.05f)) {
          transform.scale = {scale[0], scale[1]};
          transform_changed = true;
        }
        if (transform_changed) {
          if (auto set = edits.set_transform(selected_node->id(), transform); !set) {
            ARCANE_LOG_ERROR(kLogCategory, "transform edit failed: {}", set.error().message);
          }
        }

        for (const auto& component : selected_node->components()) {
          ImGui::Separator();
          ImGui::TextUnformatted(component->type_name().c_str());
          if (const auto* descriptors = registry.properties_of(component->type_name())) {
            for (const auto& descriptor : *descriptors) {
              ImGui::PushID(descriptor.name.c_str());
              if (auto value = draw_property_editor(descriptor, *component)) {
                if (auto set = edits.set_property(selected_node->id(), component->id(), descriptor, *value); !set) {
                  ARCANE_LOG_ERROR(kLogCategory, "{}.{} edit failed: {}", component->type_name(), descriptor.name,
                                   set.error().message);
                }
              }
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
                run_edit({"component.add", {{"node", node_selector(selected_node->id())}, {"type", type_name}}});
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
      // Assigns an asset id to a registered property of the selected node's component, as an undoable command.
      auto assign_asset_property = [&](const char* component_name, const char* property_name, arcane::AssetId id) {
        arcane::Node* selected_node = active_scene->find_node(*selection.selected());
        arcane::Component* component = selected_node ? selected_node->find_component(component_name) : nullptr;
        if (!component) return;
        run_edit({"prop.set", {{"node", node_selector(selected_node->id())}, {"component", component->id()},
                              {"name", property_name}, {"value", id}}});
        if (auto* animated = dynamic_cast<arcane::AnimatedSprite*>(component)) animated->reset();
      };
      auto assets = arcane::editor::list_image_assets(session.assets_dir());
      for (const auto& asset : assets) {
        const bool pressed = ImGui::Selectable(asset.relative_path.c_str());
        if (pressed && editable && selection.has_selection()) {
          // Sprite wins over ParticleEmitter when a node has both.
          const char* target = active_scene->find_node(*selection.selected()) == nullptr ? nullptr
                               : active_scene->find_node(*selection.selected())->find_component("Sprite") ? "Sprite"
                               : active_scene->find_node(*selection.selected())->find_component("ParticleEmitter")
                                   ? "ParticleEmitter" : nullptr;
          if (target) assign_asset_property(target, "texture", asset.id);
        }
      }

      // Clips and sounds: clicking one assigns it to the selected node's
      // AnimatedSprite / AudioSource (the Inspector then shows the id, and
      // "Preview animations" in the Toolbar plays it in the Viewport).
      auto assign_asset = [&](const char* header, const std::vector<arcane::editor::AssetEntry>& list,
                               const char* component_name, const char* property_name) {
        if (!ImGui::CollapsingHeader(header)) return;
        for (const auto& asset : list) {
          if (!ImGui::Selectable(asset.relative_path.c_str()) || !editable || !selection.has_selection()) continue;
          assign_asset_property(component_name, property_name, asset.id);
        }
      };
      assign_asset("Animation clips", arcane::editor::list_animation_clip_assets(session.assets_dir()),
                   "AnimatedSprite", "clip");
      assign_asset("Tilesets", arcane::editor::list_animation_clip_assets(session.assets_dir()), "Tilemap", "tileset");
      assign_asset("Sounds", arcane::editor::list_audio_assets(session.assets_dir()), "AudioSource", "clip");
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
        // Undo/redo go through the command history; the scene may change, so drop the pointers afterwards.
        if (ImGui::Button("Undo") && edits.can_undo()) {
          if (auto done = edits.undo(); !done) ARCANE_LOG_ERROR(kLogCategory, "undo failed: {}", done.error().message);
          paint_map = nullptr;
        }
        ImGui::SameLine();
        if (paint_map && ImGui::Button("Redo") && edits.can_redo()) {
          if (auto done = edits.redo(); !done) ARCANE_LOG_ERROR(kLogCategory, "redo failed: {}", done.error().message);
          paint_map = nullptr;
        }
        if (paint_map && (ImGui::Button("Add layer") || paint_map->layer_count() == 0)) {
          if (paint_map->layer_count() > 0 || !tilemap_layer_failed) {
            tilemap_layer_failed = !run_edit({"tilemap.add_layer", {{"node", node_selector(paint_node->id())}}});
          }
          paint_map = nullptr;
        }
      }
      if (paint_map) {
        ImGui::InputInt("Layer", &paint_layer);
        paint_layer = std::clamp(paint_layer, 0, std::max(0, paint_map->layer_count() - 1));
        const arcane::Tileset* tileset = tilesets.get(paint_map->tileset);
        if (!tileset) {
          ImGui::TextUnformatted("Assign a tileset from Assets > Tilesets.");
        } else {
          if (ImGui::Selectable("0: erase", paint_tile == 0)) paint_tile = 0;
          for (int id = 1; id <= tileset->tile_count; ++id) {
            const std::string label = std::to_string(id);
            if (ImGui::Selectable(label.c_str(), paint_tile == id, 0, ImVec2(28, 0))) paint_tile = id;
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
            const std::string file = std::filesystem::relative(asset.absolute_path, (*context)->project_dir()).generic_string();
            auto info = commands.execute(**context, {"tileset.info", {{"tileset", file}}});
            if (info) {
              const auto& v = info->value;
              arcane::Tileset loaded;
              loaded.texture = v["texture"]; loaded.tile_width = v["tile_width"]; loaded.tile_height = v["tile_height"];
              loaded.columns = v["columns"]; loaded.tile_count = v["tile_count"];
              loaded.tiles.resize(static_cast<std::size_t>(loaded.tile_count));
              for (const auto& tile : v["tiles"]) {
                loaded.tiles[tile["id"].get<std::size_t>() - 1] = {tile["solid"].get<bool>(), tile["terrain"].get<int>()};
              }
              for (const auto& rule : v["terrains"]) {
                arcane::TerrainRule r; r.terrain = rule["terrain"]; r.tiles = rule["tiles"].get<decltype(r.tiles)>();
                loaded.terrains.push_back(r);
              }
              tileset_draft = std::move(loaded); tileset_path = asset.absolute_path; selected_tile = 1; tileset_message.clear();
            } else tileset_message = info.error().message;
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
          // One tileset.replace through the EditSession: a single undoable step (Edit > Undo restores the old file).
          using arcane::tooling::Json;
          Json tiles = Json::array(), terrains = Json::array();
          for (std::size_t i = 0; i < tileset_draft.tiles.size(); ++i) {
            const auto& tile = tileset_draft.tiles[i];
            if (tile.solid || tile.terrain != 0) tiles.push_back({{"id", i + 1}, {"solid", tile.solid}, {"terrain", tile.terrain}});
          }
          for (const auto& rule : tileset_draft.terrains) terrains.push_back({{"terrain", rule.terrain}, {"tiles", rule.tiles}});
          const std::string file = std::filesystem::relative(tileset_path, (*context)->project_dir()).generic_string();
          auto done = edits.run({"tileset.replace",
                                 {{"tileset", file}, {"texture", tileset_draft.texture}, {"tile_width", tileset_draft.tile_width},
                                  {"tile_height", tileset_draft.tile_height}, {"columns", tileset_draft.columns},
                                  {"tile_count", tileset_draft.tile_count}, {"tiles", tiles}, {"terrains", terrains}}});
          edits.end_gesture();
          tileset_message = done ? "Saved." : "Could not save tileset: " + done.error().message;
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
                      (*context)->project_dir(), session.assets_dir(), static_cast<float>(delta_seconds),
                      [&](const arcane::tooling::Invocation& call) -> std::expected<arcane::tooling::CommandResult, std::string> {
                        // clip.info is a read; clip.create goes through the EditSession (one undoable file write).
                        auto done = call.command == "clip.info" ? commands.execute(**context, call) : edits.run(call);
                        edits.end_gesture();
                        if (!done) return std::unexpected(done.error().message);
                        return *done;
                      },
                      [&] { clips.set_assets(arcane::editor::list_animation_clip_assets(session.assets_dir())); });
      ImGui::EndDisabled();
    }
    ImGui::End();

    if (ImGui::Begin("Input Map")) {
      ImGui::BeginDisabled(!editable);
      // Every edit is an input.* command (undoable); the map is only read here. Iterate a copy because
      // a command may rebuild the map's storage while this frame is still drawing it.
      const auto actions = session.project().input_actions.actions();
      static char action_name[128] = {};
      ImGui::InputText("New action", action_name, sizeof(action_name));
      ImGui::SameLine();
      if (ImGui::Button("Add action") && action_name[0] != '\0') {
        bool exists = false;
        for (const auto& action : actions) exists |= action.name == action_name;
        if (!exists) run_edit({"input.add_action", {{"name", std::string(action_name)}}});
        action_name[0] = '\0';
      }
      static constexpr const char* kDevices[] = {"keyboard", "mouse_button", "gamepad_button", "gamepad_axis"};
      static constexpr const char* kDeviceLabels[] = {"Keyboard", "Mouse button", "Gamepad button", "Gamepad axis"};
      for (const auto& action : actions) {
        ImGui::PushID(action.name.c_str());
        if (ImGui::TreeNode(action.name.c_str())) {
          int player = action.player;
          if (ImGui::SliderInt("Player (-1 = any)", &player, -1, arcane::kMaxGamepads - 1)) {
            run_edit({"input.set_player", {{"action", action.name}, {"player", player}}}, "input:" + action.name + ":player");
          }
          for (std::size_t i = 0; i < action.bindings.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const auto& binding = action.bindings[i];
            auto set_binding = [&](const char* field, arcane::tooling::Json value) {
              run_edit({"input.set_binding", {{"action", action.name}, {"index", i}, {"field", field}, {"value", std::move(value)}}},
                       "input:" + action.name + ":" + std::to_string(i) + ":" + field);
            };
            int device = static_cast<int>(binding.device);
            if (ImGui::Combo("Device", &device, kDeviceLabels, 4)) set_binding("device", kDevices[device]);
            int code = binding.code;
            if (ImGui::InputInt("Code", &code) && code >= 0) set_binding("code", code);
            if (device >= 2) {
              int pad = binding.pad;
              if (ImGui::SliderInt("Pad (-1 = player)", &pad, -1, arcane::kMaxGamepads - 1)) set_binding("pad", pad);
            }
            if (device == 3) {
              int sign = binding.axis_sign;
              if (ImGui::SliderInt("Direction (-1 / 0 / 1)", &sign, -1, 1)) set_binding("axis_sign", sign);
              float deadzone = binding.deadzone;
              if (ImGui::SliderFloat("Deadzone", &deadzone, 0.0f, 0.99f)) set_binding("deadzone", deadzone);
              float threshold = binding.threshold;
              if (ImGui::SliderFloat("Button threshold", &threshold, 0.0f, 1.0f)) set_binding("threshold", threshold);
            }
            if (ImGui::Button("Remove binding")) {
              run_edit({"input.remove_binding", {{"action", action.name}, {"index", i}}});
              ImGui::PopID();
              break;
            }
            ImGui::Separator();
            ImGui::PopID();
          }
          if (ImGui::Button("Add binding")) run_edit({"input.add_binding", {{"action", action.name}}});
          ImGui::SameLine();
          if (ImGui::Button("Remove action")) run_edit({"input.remove_action", {{"action", action.name}}});
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      ImGui::EndDisabled();
    }
    ImGui::End();

    if (ImGui::Begin("HUD")) {
      ImGui::BeginDisabled(!editable);
      draw_hud_editor(session.project().hud, arcane::editor::list_image_assets(session.assets_dir()),
                      [&](const arcane::tooling::Invocation& call, const std::string& key) { run_edit(call, key); });
      ImGui::EndDisabled();
    }
    ImGui::End();

    if (ImGui::Begin("Project Settings")) {
      ImGui::BeginDisabled(!editable);
      // Edits are project.set commands; a field shows the project value until it is being typed in, and one
      // typing gesture (same coalesce key) is one undo step.
      auto project_field = [&](const char* label, const char* command, const char* key, const std::string& current) {
        static std::map<std::string, std::string> buffers;
        std::string& buffer = buffers[std::string(command) + key];
        if (buffer.empty() || !ImGui::IsAnyItemActive()) buffer = current;
        char text[512] = {};
        std::snprintf(text, sizeof(text), "%s", buffer.c_str());
        if (ImGui::InputText(label, text, sizeof(text))) {
          buffer = text;
          run_edit({command, {{key, buffer}}}, std::string(command) + "." + key);
        }
        if (ImGui::IsItemDeactivated()) edits.end_gesture();
      };
      const auto& project = session.project();
      project_field("Name", "project.set", "name", project.name);
      project_field("Main scene", "project.set", "main_scene", project.main_scene);
      project_field("Module", "project.set", "module", project.module);
      ImGui::TextDisabled("Main scene and module take effect on next open.");

      // Scene transition, save and debug settings: transition.set / save.config_set / debug.set, in the same history.
      using arcane::tooling::Json;
      ImGui::SeparatorText("Scene transition");
      float fade_out = project.transition.fade_out_seconds, fade_in = project.transition.fade_in_seconds;
      if (ImGui::DragFloat("Fade out (s)", &fade_out, 0.01f, 0.0f, 60.0f)) {
        run_edit({"transition.set", {{"fade_out_seconds", fade_out}}}, "transition.fade_out");
      }
      if (ImGui::IsItemDeactivated()) edits.end_gesture();
      if (ImGui::DragFloat("Fade in (s)", &fade_in, 0.01f, 0.0f, 60.0f)) {
        run_edit({"transition.set", {{"fade_in_seconds", fade_in}}}, "transition.fade_in");
      }
      if (ImGui::IsItemDeactivated()) edits.end_gesture();
      float fade_color[4] = {project.transition.color.r, project.transition.color.g, project.transition.color.b, project.transition.color.a};
      if (ImGui::ColorEdit4("Fade colour", fade_color)) {
        run_edit({"transition.set", {{"color", Json::array({fade_color[0], fade_color[1], fade_color[2], fade_color[3]})}}},
                 "transition.color");
      }
      if (ImGui::IsItemDeactivated()) edits.end_gesture();

      ImGui::SeparatorText("Save");
      project_field("Game id", "save.config_set", "game_id", project.save.game_id);
      int data_version = project.save.data_version;
      if (ImGui::InputInt("Data version", &data_version) && data_version >= 1) {
        run_edit({"save.config_set", {{"data_version", data_version}}}, "save.data_version");
      }
      if (ImGui::IsItemDeactivated()) edits.end_gesture();

      ImGui::SeparatorText("Debug overlay");
      bool overlay_visible = project.debug.overlay_visible;
      if (ImGui::Checkbox("Visible at start", &overlay_visible)) run_edit({"debug.set", {{"overlay_visible", overlay_visible}}});
      float font_scale = project.debug.overlay_font_scale;
      if (ImGui::DragFloat("Font scale", &font_scale, 0.01f, 0.1f, 8.0f)) {
        run_edit({"debug.set", {{"overlay_font_scale", font_scale}}}, "debug.font_scale");
      }
      if (ImGui::IsItemDeactivated()) edits.end_gesture();
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
        const std::string target = node_selector(paint_node->id());
        // Cell under a world position, not clamped to the grid (rectangles clamp afterwards).
        auto cell_of = [&](arcane::Vec2 at, int& cx, int& cy) {
          if (paint_map->tile_width <= 0.0f || paint_map->tile_height <= 0.0f || node_world.scale.x == 0.0f ||
              node_world.scale.y == 0.0f) {
            return false;
          }
          cx = static_cast<int>(std::floor((at.x - node_world.position.x) / node_world.scale.x / paint_map->tile_width));
          cy = static_cast<int>(std::floor((at.y - node_world.position.y) / node_world.scale.y / paint_map->tile_height));
          return true;
        };
        if (ImGui::IsItemHovered() && (left || right)) {
          paint_stroke = true;
          if (paint_rect && !terrain_mode) {
            if (!rect_start) {
              rect_start = world;
              rect_erase = right;
            }
          } else {
            int cx = 0, cy = 0;
            if (cell_of(world, cx, cy) && paint_map->in_bounds(cx, cy) && paint_map->layer_count() > 0) {
              arcane::tooling::Json cell = {{"node", target}, {"layer", paint_layer}, {"x", cx}, {"y", cy}};
              const char* command = "tilemap.paint";
              if (terrain_mode) {
                command = "tilemap.terrain_paint";
                cell["terrain"] = paint_terrain;
                cell["erase"] = right;
              } else if (right || paint_tile == 0) {
                command = "tilemap.erase";
              } else {
                cell["tile"] = paint_tile;
              }
              auto done = edits.run({command, cell}, "tile-stroke");
              // Painting a tile the tileset lacks, or terrain without a rule, is simply ignored while dragging.
              if (!done) ARCANE_LOG_DEBUG(kLogCategory, "{}: {}", command, done.error().message);
            }
          }
        } else if (!left && !right) {
          if (rect_start) {
            int ax = 0, ay = 0, bx = 0, by = 0;
            if (cell_of(*rect_start, ax, ay) && cell_of(world, bx, by) && paint_map->layer_count() > 0) {
              const int x0 = std::max(0, std::min(ax, bx)), x1 = std::min(paint_map->width - 1, std::max(ax, bx));
              const int y0 = std::max(0, std::min(ay, by)), y1 = std::min(paint_map->height - 1, std::max(ay, by));
              if (x0 <= x1 && y0 <= y1) {
                auto done = edits.run({"tilemap.fill_rect", {{"node", target}, {"layer", paint_layer}, {"x0", x0}, {"y0", y0},
                                                              {"x1", x1}, {"y1", y1}, {"tile", rect_erase ? 0 : paint_tile}}});
                if (!done) ARCANE_LOG_ERROR(kLogCategory, "fill failed: {}", done.error().message);
              }
            }
            rect_start.reset();
          }
          if (paint_stroke) edits.end_gesture();  // mouse released: the next stroke is a new undo step
          paint_stroke = false;
        }
      }
    }
    ImGui::End();

    // Debug overlay (F3): the same numbers as the player's, from project.json "debug" at startup.
    frame_timer.tick(delta_seconds);
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) overlay_visible = !overlay_visible;
    if (overlay_visible) {
      const arcane::RenderStats& render_stats = renderer->stats();
      const arcane::FrameStats stats{frame_timer.fps(), frame_timer.frame_ms(), render_stats.draw_calls,
                                     render_stats.sprites, play_stop.physics_body_count(),
                                     arcane::count_particles(*active_scene)};
      ImGui::SetNextWindowBgAlpha(0.6f);
      if (ImGui::Begin("Debug overlay (F3)", &overlay_visible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(arcane::format_debug_overlay(stats).c_str());
      }
      ImGui::End();
    }

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
