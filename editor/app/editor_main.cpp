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
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "arcane/builtin_components.hpp"
#include "arcane/log.hpp"
#include "arcane/module.hpp"
#include "arcane/node.hpp"
#include "arcane/renderer.hpp"
#include "arcane/scene.hpp"
#include "arcane/time.hpp"
#include "arcane/window.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_editor/editor_state.hpp"
#include "arcane_editor/project_session.hpp"

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

void visit_preorder(arcane::Node* node, const std::function<void(arcane::Node*)>& visit) {
  visit(node);
  for (const auto& child : node->children()) {
    visit_preorder(child.get(), visit);
  }
}

void visit_scene_preorder(arcane::Scene& scene, const std::function<void(arcane::Node*)>& visit) {
  for (const auto& root : scene.roots()) {
    visit_preorder(root.get(), visit);
  }
}

// Resolves and caches the GPU textures backing Sprite::texture asset ids.
class TextureCache {
 public:
  void set_assets(std::vector<arcane::editor::AssetEntry> assets) { assets_ = std::move(assets); }

  arcane::Texture* get(arcane::AssetId id) {
    if (id == 0) return nullptr;
    auto it = textures_.find(id);
    if (it != textures_.end()) return &it->second;

    for (const auto& entry : assets_) {
      if (entry.id != id) continue;
      auto loaded = arcane::Texture::load_from_file(entry.absolute_path, arcane::TextureFilter::Nearest);
      if (!loaded) {
        ARCANE_LOG_WARN(kLogCategory, "failed to load asset '{}': {}", entry.relative_path, loaded.error());
        return nullptr;
      }
      auto [inserted, _] = textures_.emplace(id, std::move(*loaded));
      return &inserted->second;
    }
    return nullptr;
  }

 private:
  std::vector<arcane::editor::AssetEntry> assets_;
  std::map<arcane::AssetId, arcane::Texture> textures_;
};

void render_viewport_scene(arcane::Scene& scene, arcane::SpriteRenderer& renderer, TextureCache& textures, int width,
                            int height) {
  float cam_x = 0.0f, cam_y = 0.0f, cam_zoom = 1.0f;
  visit_scene_preorder(scene, [&](arcane::Node* node) {
    auto* camera = static_cast<arcane::Camera*>(node->find_component("Camera"));
    if (camera && camera->primary) {
      const auto world = node->world_transform();
      cam_x = world.position.x;
      cam_y = world.position.y;
      cam_zoom = camera->zoom;
    }
  });

  renderer.set_camera(cam_x, cam_y, cam_zoom);
  renderer.begin_frame(width, height, arcane::Color{0.12f, 0.12f, 0.16f, 1.0f});

  visit_scene_preorder(scene, [&](arcane::Node* node) {
    auto* sprite = static_cast<arcane::Sprite*>(node->find_component("Sprite"));
    if (!sprite) return;
    arcane::Texture* texture = textures.get(sprite->texture);
    if (!texture) return;
    const auto world = node->world_transform();
    renderer.draw(arcane::SpriteDraw{
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

  // Declared before `registry`: registry's PropertyDescriptors hold
  // std::function closures compiled into the game module's .so, so the
  // module must still be mapped in when those closures are destroyed.
  // Reverse-declaration-order destruction means registry (declared later)
  // is torn down first, then module unloads (dlclose) safely afterwards.
  std::optional<arcane::GameModule> module;

  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);

  auto session = arcane::editor::ProjectSession::open(args.project_dir, registry);
  if (!session) {
    // The project may reference a module whose types the registry doesn't
    // know about yet; load it (if declared) before re-trying.
    ARCANE_LOG_WARN(kLogCategory, "initial project open failed ({}); retrying after module load", session.error());
  }

  // Resolve the module path: an explicit --module always wins; otherwise
  // peek at project.json's "module" field, even if the first open() above
  // failed because of unknown component types.
  {
    std::optional<std::filesystem::path> module_path;
    if (!args.module_override.empty()) {
      module_path = args.module_override;
    } else {
      auto probe = arcane::load_project_from_json([&] {
        std::ifstream in(std::filesystem::path(args.project_dir) / "project.json");
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
      }());
      if (probe && !probe->module.empty()) {
        module_path = std::filesystem::path(args.project_dir) / probe->module;
      }
    }
    if (module_path) {
      auto loaded = arcane::GameModule::load(*module_path);
      if (loaded) {
        module = std::move(*loaded);
        if (module->api()->register_types) {
          module->api()->register_types(registry);
        }
        module->api()->on_load();
      } else {
        ARCANE_LOG_ERROR(kLogCategory, "failed to load project module '{}': {}", module_path->string(),
                          loaded.error());
      }
    }
  }

  if (!session) {
    session = arcane::editor::ProjectSession::open(args.project_dir, registry);
  }
  if (!session) {
    ARCANE_LOG_ERROR(kLogCategory, "failed to open project '{}': {}", args.project_dir, session.error());
    return 1;
  }

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
  TextureCache textures;
  textures.set_assets(arcane::editor::list_image_assets(session->assets_dir()));

  ViewportTarget viewport;
  arcane::FixedTimestep timestep(60.0);
  auto previous_time = std::chrono::steady_clock::now();
  long frame_count = 0;
  long play_tick_count = 0;

  while (!window->should_close()) {
    if (args.frames.has_value() && frame_count >= *args.frames) break;

    window->poll_events();

    if (args.play_frames.has_value() && frame_count == 0) {
      play_stop.play(session->scene(), registry);
    }

    const auto now = std::chrono::steady_clock::now();
    const double delta_seconds = std::chrono::duration<double>(now - previous_time).count();
    previous_time = now;
    const int steps = timestep.advance(delta_seconds);
    if (play_stop.mode() == arcane::editor::EditorMode::Play) {
      for (int i = 0; i < steps; ++i) {
        play_stop.tick(static_cast<float>(timestep.fixed_delta_seconds()));
        ++play_tick_count;
      }
      if (args.play_frames.has_value() && play_tick_count >= *args.play_frames) {
        play_stop.stop();
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save")) {
          auto saved = session->save(registry);
          if (!saved) ARCANE_LOG_ERROR(kLogCategory, "save failed: {}", saved.error());
        }
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    if (ImGui::Begin("Toolbar")) {
      const bool playing = play_stop.mode() == arcane::editor::EditorMode::Play;
      if (!playing) {
        if (ImGui::Button("Play")) play_stop.play(session->scene(), registry);
      } else {
        if (ImGui::Button("Stop")) play_stop.stop();
      }
      ImGui::SameLine();
      ImGui::TextUnformatted(playing ? "Mode: Play" : "Mode: Edit");
      ImGui::SameLine();
      ImGui::TextUnformatted(session->dirty() ? "*modified*" : "");
    }
    ImGui::End();

    arcane::Scene* active_scene = play_stop.active_scene(session->scene());
    const bool editable = play_stop.mode() == arcane::editor::EditorMode::Edit;

    if (ImGui::Begin("Hierarchy")) {
      if (editable && ImGui::Button("New Node")) {
        arcane::Node* parent = nullptr;
        if (selection.has_selection()) parent = active_scene->find_node(*selection.selected());
        arcane::Node* created = active_scene->create_node("Node", parent);
        selection.select(created->id());
        session->mark_dirty();
      }
      ImGui::SameLine();
      if (editable && selection.has_selection() && ImGui::Button("Delete")) {
        active_scene->destroy_node(*selection.selected());
        active_scene->flush_destroyed();
        selection.clear();
        session->mark_dirty();
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
            if (dragged_node && active_scene->reparent(dragged_node, node)) session->mark_dirty();
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
          session->mark_dirty();
        }
        auto& transform = selected_node->local_transform();
        float pos[2] = {transform.position.x, transform.position.y};
        if (ImGui::DragFloat2("Position", pos, 0.5f)) {
          transform.position = {pos[0], pos[1]};
          session->mark_dirty();
        }
        float rotation_degrees = transform.rotation_radians * 180.0f / 3.14159265f;
        if (ImGui::DragFloat("Rotation", &rotation_degrees, 1.0f)) {
          transform.rotation_radians = rotation_degrees * 3.14159265f / 180.0f;
          session->mark_dirty();
        }
        float scale[2] = {transform.scale.x, transform.scale.y};
        if (ImGui::DragFloat2("Scale", scale, 0.05f)) {
          transform.scale = {scale[0], scale[1]};
          session->mark_dirty();
        }

        for (const auto& component : selected_node->components()) {
          ImGui::Separator();
          ImGui::TextUnformatted(component->type_name().c_str());
          if (const auto* descriptors = registry.properties_of(component->type_name())) {
            for (const auto& descriptor : *descriptors) {
              ImGui::PushID(descriptor.name.c_str());
              if (draw_property_editor(descriptor, *component)) session->mark_dirty();
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
                session->mark_dirty();
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
      auto assets = arcane::editor::list_image_assets(session->assets_dir());
      for (const auto& asset : assets) {
        const bool pressed = ImGui::Selectable(asset.relative_path.c_str());
        if (pressed && editable && selection.has_selection()) {
          arcane::Node* selected_node = active_scene->find_node(*selection.selected());
          auto* sprite = selected_node ? static_cast<arcane::Sprite*>(selected_node->find_component("Sprite"))
                                        : nullptr;
          if (sprite) {
            sprite->texture = asset.id;
            session->mark_dirty();
          }
        }
      }
    }
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Viewport")) {
      ImVec2 size = ImGui::GetContentRegionAvail();
      const int vw = std::max(1, static_cast<int>(size.x));
      const int vh = std::max(1, static_cast<int>(size.y));
      viewport.ensure_size(vw, vh);

      viewport.bind();
      render_viewport_scene(*active_scene, *renderer, textures, viewport.width(), viewport.height());
      ViewportTarget::unbind();

      ImGui::Image(static_cast<ImTextureID>(static_cast<std::uintptr_t>(viewport.texture())),
                    ImVec2(static_cast<float>(viewport.width()), static_cast<float>(viewport.height())),
                    ImVec2(0, 1), ImVec2(1, 0));
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

  if (module) {
    module->api()->on_unload();
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  ARCANE_LOG_INFO(kLogCategory, "exiting after {} frames", frame_count);
  return 0;
}
