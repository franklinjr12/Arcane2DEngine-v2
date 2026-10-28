// arcane_editor: the Foundation C minimal integrated editor. Dear ImGui
// (docking) + GLFW/OpenGL3, built on top of arcane_editor_core (selection,
// Play/Stop isolation, project session) and arcane_runtime (scene, sprite
// renderer, component registry). See docs/decisions.md for the viewport
// offscreen-framebuffer and asset-id design choices.
#include <glad/gl.h>

#include <GLFW/glfw3.h>
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
#include <optional>
#include <string>
#include <vector>

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
  bool paint_mode = false;
  int paint_layer = 0;
  int paint_tile = 1;  // 0 erases
  auto hud_renderer = arcane::HudRenderer::create();
  if (!hud_renderer) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create HUD renderer: {}", hud_renderer.error());
    return 1;
  }
  arcane::Hud hud;  // only live (and drawn) while Play is running; copied from the project HUD
  arcane::HudInput hud_input;
  arcane::Vec2 viewport_origin{};  // top-left of the Viewport image in window coords (previous frame)
  int viewport_w = 1, viewport_h = 1;

  // Null backend: the editor never opens an audio device, but play_on_start
  // and one-shot calls are still recorded. See docs/decisions.md.
  auto audio = arcane::AudioSystem::create(/*null_backend=*/true);
  if (!audio) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to create audio system: {}", audio.error());
    return 1;
  }
  auto begin_play = [&] {
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
          if (sprite) {
            sprite->texture = asset.id;
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
        if (paint_map->layer_count() == 0 || ImGui::Button("Add layer")) {
          paint_map->add_layer();
          session.mark_dirty();
        }
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
                                  &tilesets);
      ViewportTarget::unbind();

      ImGui::Image(static_cast<ImTextureID>(static_cast<std::uintptr_t>(viewport.texture())),
                    ImVec2(static_cast<float>(viewport.width()), static_cast<float>(viewport.height())),
                    ImVec2(0, 1), ImVec2(1, 0));
      const ImVec2 image_min = ImGui::GetItemRectMin();
      viewport_origin = {image_min.x, image_min.y};
      viewport_w = viewport.width();
      viewport_h = viewport.height();

      if (paint_mode && paint_map && ImGui::IsItemHovered() &&
          (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right))) {
        const ImVec2 origin = ImGui::GetItemRectMin();
        const ImVec2 mouse = ImGui::GetMousePos();
        const arcane::Vec2 world =
            arcane::editor::viewport_to_world(*active_scene, mouse.x - origin.x, mouse.y - origin.y, viewport.width(),
                                              viewport.height());
        const int id = ImGui::IsMouseDown(ImGuiMouseButton_Right) ? 0 : paint_tile;
        if (arcane::editor::paint_tile_at_world(*paint_map, paint_node->world_transform(), world, paint_layer, id)) {
          session.mark_dirty();
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
