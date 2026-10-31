#include "arcane/scene_flow.hpp"

#include "arcane/prefab.hpp"

#include <algorithm>

#include "arcane/atlas.hpp"
#include "arcane/canonical_json.hpp"

namespace arcane {

namespace {

SceneManager* g_active_manager = nullptr;
constexpr const char* kFadeId = "scene_fade";
constexpr float kFadeSize = 20000.0f;  // layout units: covers any screen

}  // namespace

void set_active_scene_manager(SceneManager* manager) noexcept { g_active_manager = manager; }
SceneManager* active_scene_manager() noexcept { return g_active_manager; }

SceneManager::SceneManager(std::filesystem::path project_dir, const ComponentRegistry& registry,
                           TransitionConfig config)
    : project_dir_(std::move(project_dir)), registry_(&registry), config_(config) {}

SceneManager::~SceneManager() = default;

void SceneManager::set_scene(Scene scene) {
  scene_ = std::make_unique<Scene>(std::move(scene));
  persistent_.clear();
}

bool SceneManager::set_persistent(NodeId id, bool persistent) {
  if (!scene_) return false;
  Node* node = scene_->find_node(id);
  if (!node || node->parent()) return false;
  if (persistent) persistent_.insert(id);
  else persistent_.erase(id);
  return true;
}

Node* SceneManager::find_persistent(const std::string& name) const {
  if (!scene_) return nullptr;
  for (const auto& root : scene_->roots()) {
    if (root->name() == name && persistent_.contains(root->id()) && !root->pending_destroy()) return root.get();
  }
  return nullptr;
}

std::expected<void, std::string> SceneManager::switch_now(const std::string& scene_path) {
  if (!is_project_relative_path(scene_path)) {
    return std::unexpected("scene path '" + scene_path + "' is not a project-relative '/' path");
  }
  auto text = read_text_file(project_dir_ / scene_path);
  if (!text) return std::unexpected(text.error());
  auto next = Scene::load_from_json(*text, *registry_);
  if (!next) return std::unexpected(scene_path + ": " + next.error());
  refresh_prefab_instances(*next, project_dir_, *registry_);

  auto incoming = std::make_unique<Scene>(std::move(*next));
  std::unordered_set<NodeId> carried;
  if (scene_) {
    // Move the persistent roots first; everything left is torn down properly.
    std::vector<Node*> keep;
    for (const auto& root : scene_->roots()) {
      if (persistent_.contains(root->id())) keep.push_back(root.get());
    }
    for (Node* node : keep) {
      if (Node* moved = incoming->adopt_root(*scene_, node, *registry_)) carried.insert(moved->id());
    }
    for (const auto& root : scene_->roots()) scene_->destroy_node(root->id());
    scene_->flush_destroyed();
  }
  scene_ = std::move(incoming);
  persistent_ = std::move(carried);
  current_path_ = scene_path;
  if (on_switched_) on_switched_(*scene_);
  return {};
}

void SceneManager::request_switch(std::string scene_path, bool fade) {
  pending_ = std::move(scene_path);
  pending_fade_ = fade;
}

void SceneManager::perform_queued_switch() {
  const std::string path = std::move(pending_);
  pending_.clear();
  if (auto ok = switch_now(path); !ok) last_error_ = ok.error();
  else last_error_.clear();
}

void SceneManager::update(float dt) {
  if (phase_ == Phase::Idle && !pending_.empty()) {
    queued_fade_ = pending_fade_;
    timer_ = 0.0f;
    if (queued_fade_ && config_.fade_out_seconds > 0.0f) {
      phase_ = Phase::FadingOut;
    } else {
      perform_queued_switch();
      phase_ = queued_fade_ && config_.fade_in_seconds > 0.0f ? Phase::FadingIn : Phase::Idle;
    }
  } else if (phase_ == Phase::FadingOut) {
    timer_ += dt;
    if (timer_ >= config_.fade_out_seconds) {
      perform_queued_switch();
      timer_ = 0.0f;
      phase_ = config_.fade_in_seconds > 0.0f ? Phase::FadingIn : Phase::Idle;
    }
  } else if (phase_ == Phase::FadingIn) {
    timer_ += dt;
    if (timer_ >= config_.fade_in_seconds) phase_ = Phase::Idle;
  }

  switch (phase_) {
    case Phase::FadingOut: alpha_ = std::clamp(timer_ / config_.fade_out_seconds, 0.0f, 1.0f); break;
    case Phase::FadingIn: alpha_ = 1.0f - std::clamp(timer_ / config_.fade_in_seconds, 0.0f, 1.0f); break;
    case Phase::Idle: alpha_ = 0.0f; break;
  }
  apply_fade_overlay();
}

void SceneManager::apply_fade_overlay() {
  Hud* hud = hud_ ? hud_ : active_hud();
  if (!hud) return;
  auto& elements = hud->elements();
  std::erase_if(elements, [](const HudElement& e) {
    const auto* bar = std::get_if<HudProgressBar>(&e);
    return bar && bar->id == kFadeId;
  });
  if (alpha_ <= 0.0f) return;
  HudProgressBar overlay;
  overlay.id = kFadeId;
  overlay.anchor = HudAnchor::Center;
  overlay.width = overlay.height = kFadeSize;
  overlay.fraction = 1.0f;
  overlay.fill = Color{config_.color.r, config_.color.g, config_.color.b, config_.color.a * alpha_};
  overlay.background = Color{0.0f, 0.0f, 0.0f, 0.0f};
  hud->add(overlay);  // last = drawn on top
}

}  // namespace arcane
