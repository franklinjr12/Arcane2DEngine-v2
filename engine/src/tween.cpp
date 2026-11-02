#include "arcane/tween.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/scene.hpp"

namespace arcane {

namespace {

// Slack for fixed steps whose float dt does not sum exactly to the duration.
constexpr double kTimeEpsilon = 1e-5;

constexpr std::pair<Easing, std::string_view> kEasingNames[] = {
    {Easing::Linear, "linear"},       {Easing::QuadIn, "quad_in"},     {Easing::QuadOut, "quad_out"},
    {Easing::QuadInOut, "quad_in_out"}, {Easing::CubicIn, "cubic_in"}, {Easing::CubicOut, "cubic_out"},
    {Easing::CubicInOut, "cubic_in_out"}, {Easing::SineIn, "sine_in"}, {Easing::SineOut, "sine_out"},
    {Easing::SineInOut, "sine_in_out"}, {Easing::BackIn, "back_in"}, {Easing::BackOut, "back_out"},
    {Easing::BackInOut, "back_in_out"}, {Easing::ElasticIn, "elastic_in"}, {Easing::ElasticOut, "elastic_out"},
    {Easing::ElasticInOut, "elastic_in_out"}, {Easing::BounceIn, "bounce_in"}, {Easing::BounceOut, "bounce_out"},
    {Easing::BounceInOut, "bounce_in_out"},
};

constexpr float kBackC1 = 1.70158f;

float bounce_out(float t) noexcept {
  constexpr float n = 7.5625f, d = 2.75f;
  if (t < 1.0f / d) return n * t * t;
  if (t < 2.0f / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
  if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
  t -= 2.625f / d;
  return n * t * t + 0.984375f;
}

bool node_alive(const Scene* scene, NodeId id) {
  if (scene == nullptr) return true;
  const Node* node = scene->find_node(id);
  return node != nullptr && !node->pending_destroy();
}

}  // namespace

float ease(Easing easing, float t) noexcept {
  t = std::clamp(t, 0.0f, 1.0f);
  if (t == 0.0f || t == 1.0f) return t;  // exact endpoints for every curve
  constexpr float pi = std::numbers::pi_v<float>;
  switch (easing) {
    case Easing::Linear: return t;
    case Easing::QuadIn: return t * t;
    case Easing::QuadOut: return t * (2.0f - t);
    case Easing::QuadInOut: return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
    case Easing::CubicIn: return t * t * t;
    case Easing::CubicOut: { const float u = t - 1.0f; return u * u * u + 1.0f; }
    case Easing::CubicInOut: {
      if (t < 0.5f) return 4.0f * t * t * t;
      const float u = 2.0f * t - 2.0f;
      return 0.5f * u * u * u + 1.0f;
    }
    case Easing::SineIn: return 1.0f - std::cos(t * pi * 0.5f);
    case Easing::SineOut: return std::sin(t * pi * 0.5f);
    case Easing::SineInOut: return 0.5f * (1.0f - std::cos(pi * t));
    case Easing::BackIn: return (kBackC1 + 1.0f) * t * t * t - kBackC1 * t * t;
    case Easing::BackOut: {
      const float u = t - 1.0f;
      return 1.0f + (kBackC1 + 1.0f) * u * u * u + kBackC1 * u * u;
    }
    case Easing::BackInOut: {
      const float c2 = kBackC1 * 1.525f;
      if (t < 0.5f) { const float u = 2.0f * t; return 0.5f * u * u * ((c2 + 1.0f) * u - c2); }
      const float u = 2.0f * t - 2.0f;
      return 0.5f * (u * u * ((c2 + 1.0f) * u + c2) + 2.0f);
    }
    case Easing::ElasticIn: return -std::exp2(10.0f * t - 10.0f) * std::sin((10.0f * t - 10.75f) * (2.0f * pi / 3.0f));
    case Easing::ElasticOut: return std::exp2(-10.0f * t) * std::sin((10.0f * t - 0.75f) * (2.0f * pi / 3.0f)) + 1.0f;
    case Easing::ElasticInOut: {
      const float c5 = 2.0f * pi / 4.5f;
      const float s = std::sin((20.0f * t - 11.125f) * c5);
      return t < 0.5f ? -0.5f * std::exp2(20.0f * t - 10.0f) * s : 0.5f * std::exp2(-20.0f * t + 10.0f) * s + 1.0f;
    }
    case Easing::BounceIn: return 1.0f - bounce_out(1.0f - t);
    case Easing::BounceInOut:
      return t < 0.5f ? 0.5f * (1.0f - bounce_out(1.0f - 2.0f * t)) : 0.5f * (1.0f + bounce_out(2.0f * t - 1.0f));
    case Easing::BounceOut: return bounce_out(t);
  }
  return t;
}

std::string_view to_string(Easing easing) noexcept {
  for (const auto& [value, name] : kEasingNames) {
    if (value == easing) return name;
  }
  return "linear";
}

std::optional<Easing> parse_easing(std::string_view name) noexcept {
  for (const auto& [value, text] : kEasingNames) {
    if (text == name) return value;
  }
  return std::nullopt;
}

TweenValue evaluate_tween(const TweenSpec& spec, double elapsed_seconds) noexcept {
  if (elapsed_seconds + kTimeEpsilon < spec.delay) return {spec.from, false, false};
  const double local = std::max(0.0, elapsed_seconds - spec.delay);
  const int loops = std::max(spec.loops, 0);
  const bool done = spec.duration <= 0.0f || (loops > 0 && local + kTimeEpsilon >= double(spec.duration) * loops);
  if (done) {
    const bool backwards = spec.yoyo && loops > 0 && (loops - 1) % 2 == 1;
    return {backwards ? spec.from : spec.to, true, true};
  }
  const double pass = std::floor(local / spec.duration);
  float t = float((local - pass * spec.duration) / spec.duration);
  if (spec.yoyo && std::fmod(pass, 2.0) == 1.0) t = 1.0f - t;
  return {spec.from + (spec.to - spec.from) * ease(spec.easing, t), true, false};
}

bool is_node_tween_target(std::string_view target) noexcept {
  return target == "position.x" || target == "position.y" || target == "rotation" || target == "scale.x" ||
         target == "scale.y" || target == "tint.r" || target == "tint.g" || target == "tint.b" ||
         target == "tint.a";
}

namespace {
// Pointer to the float a target names, or nullptr (unknown name / no Sprite).
float* target_slot(Node& node, std::string_view target) {
  Transform2D& t = node.local_transform();
  if (target == "position.x") return &t.position.x;
  if (target == "position.y") return &t.position.y;
  if (target == "rotation") return &t.rotation_radians;
  if (target == "scale.x") return &t.scale.x;
  if (target == "scale.y") return &t.scale.y;
  if (target.starts_with("tint.") && target.size() == 6) {
    auto* sprite = dynamic_cast<Sprite*>(node.find_component("Sprite"));
    if (sprite == nullptr) return nullptr;
    switch (target[5]) {
      case 'r': return &sprite->tint.r;
      case 'g': return &sprite->tint.g;
      case 'b': return &sprite->tint.b;
      case 'a': return &sprite->tint.a;
    }
  }
  return nullptr;
}
}  // namespace

std::optional<float> read_node_tween_target(const Node& node, std::string_view target) {
  const float* slot = target_slot(const_cast<Node&>(node), target);
  if (slot == nullptr) return std::nullopt;
  return *slot;
}

bool write_node_tween_target(Node& node, std::string_view target, float value) {
  float* slot = target_slot(node, target);
  if (slot == nullptr) return false;
  *slot = value;
  return true;
}

// --- TweenSystem -----------------------------------------------------------

struct TweenSystem::Impl {
  // A single tween is a chain of one step. `offset` is when the step's own
  // clock starts, in seconds since the chain started.
  struct Step {
    TweenSpec spec;
    std::function<void(float)> apply;
    double offset = 0.0;
    bool done = false;
  };
  struct Entry {
    TweenId id = 0;
    std::vector<Step> steps;
    double elapsed = 0.0;
    std::function<void()> on_complete;
    Scene* scene = nullptr;  // set with `node` for node-bound tweens
    NodeId node = kInvalidNodeId;
    bool dead = false;
  };
  // Stable addresses: callbacks may start or cancel tweens mid-update.
  std::vector<std::unique_ptr<Entry>> entries;
  TweenId next_id = 1;

  Entry* add(std::vector<TweenStep>&& steps, bool sequence, std::function<void()>&& on_complete) {
    auto entry = std::make_unique<Entry>();
    entry->id = next_id++;
    entry->on_complete = std::move(on_complete);
    double offset = 0.0;
    for (TweenStep& s : steps) {
      entry->steps.push_back({s.spec, std::move(s.apply), sequence ? offset : 0.0, false});
      offset += double(std::max(s.spec.delay, 0.0f)) + double(std::max(s.spec.duration, 0.0f)) * std::max(s.spec.loops, 0);
    }
    entries.push_back(std::move(entry));
    return entries.back().get();
  }

  Entry* find(TweenId id) {
    for (auto& e : entries) {
      if (e->id == id && !e->dead) return e.get();
    }
    return nullptr;
  }

  static void snap(Entry& e) {
    for (Step& s : e.steps) {
      if (s.done || !s.apply || (s.spec.loops == 0 && s.spec.duration > 0.0f)) continue;
      s.apply(evaluate_tween(s.spec, double(s.spec.delay) + double(s.spec.duration) * std::max(s.spec.loops, 1) + 1.0).value);
    }
  }

  static void kill(Entry& e, bool snap_to_end) {
    e.dead = true;
    if (snap_to_end) snap(e);
  }

  TweenId start_chain(std::vector<TweenStep> steps, bool sequence, std::function<void()> on_complete) {
    if (steps.empty()) return 0;
    if (sequence) {
      for (std::size_t i = 0; i + 1 < steps.size(); ++i) {
        if (steps[i].spec.loops == 0 && steps[i].spec.duration > 0.0f) return 0;
      }
    }
    return add(std::move(steps), sequence, std::move(on_complete))->id;
  }
};

TweenSystem::TweenSystem() : impl_(std::make_unique<Impl>()) {}
TweenSystem::~TweenSystem() = default;
TweenSystem::TweenSystem(TweenSystem&&) noexcept = default;
TweenSystem& TweenSystem::operator=(TweenSystem&&) noexcept = default;

TweenId TweenSystem::start(const TweenSpec& spec, std::function<void(float)> apply,
                           std::function<void()> on_complete) {
  std::vector<TweenStep> steps;
  steps.push_back({spec, std::move(apply)});
  return impl_->add(std::move(steps), false, std::move(on_complete))->id;
}

TweenId TweenSystem::start_sequence(std::vector<TweenStep> steps, std::function<void()> on_complete) {
  return impl_->start_chain(std::move(steps), true, std::move(on_complete));
}

TweenId TweenSystem::start_parallel(std::vector<TweenStep> steps, std::function<void()> on_complete) {
  return impl_->start_chain(std::move(steps), false, std::move(on_complete));
}

TweenId TweenSystem::start_node(Scene& scene, NodeId node, std::string_view target, const TweenSpec& spec,
                                std::function<void()> on_complete) {
  const Node* found = scene.find_node(node);
  if (found == nullptr || found->pending_destroy() || !read_node_tween_target(*found, target)) return 0;
  const TweenId id = start(
      spec,
      [&scene, node, name = std::string(target)](float value) {
        if (Node* n = scene.find_node(node)) write_node_tween_target(*n, name, value);
      },
      std::move(on_complete));
  impl_->entries.back()->scene = &scene;
  impl_->entries.back()->node = node;
  return id;
}

namespace {
std::vector<TweenStep> node_steps(Scene& scene, NodeId node, const std::vector<NodeTweenStep>& steps) {
  std::vector<TweenStep> out;
  const Node* found = scene.find_node(node);
  if (found == nullptr || found->pending_destroy()) return out;
  for (const NodeTweenStep& s : steps) {
    if (!read_node_tween_target(*found, s.target)) return {};
    out.push_back({s.spec, [&scene, node, name = s.target](float value) {
                     if (Node* n = scene.find_node(node)) write_node_tween_target(*n, name, value);
                   }});
  }
  return out;
}
}  // namespace

TweenId TweenSystem::start_node_sequence(Scene& scene, NodeId node, const std::vector<NodeTweenStep>& steps,
                                         std::function<void()> on_complete) {
  const TweenId id = impl_->start_chain(node_steps(scene, node, steps), true, std::move(on_complete));
  if (id != 0) { impl_->entries.back()->scene = &scene; impl_->entries.back()->node = node; }
  return id;
}

TweenId TweenSystem::start_node_parallel(Scene& scene, NodeId node, const std::vector<NodeTweenStep>& steps,
                                         std::function<void()> on_complete) {
  const TweenId id = impl_->start_chain(node_steps(scene, node, steps), false, std::move(on_complete));
  if (id != 0) { impl_->entries.back()->scene = &scene; impl_->entries.back()->node = node; }
  return id;
}

bool TweenSystem::cancel(TweenId id, bool snap_to_end) {
  Impl::Entry* e = impl_->find(id);
  if (e == nullptr) return false;
  Impl::kill(*e, snap_to_end);
  return true;
}

std::size_t TweenSystem::cancel_node(NodeId node, bool snap_to_end) {
  std::size_t count = 0;
  if (node == kInvalidNodeId) return 0;
  for (auto& e : impl_->entries) {
    if (e->node == node && !e->dead) { Impl::kill(*e, snap_to_end); ++count; }
  }
  return count;
}

bool TweenSystem::active(TweenId id) const {
  return std::ranges::any_of(impl_->entries, [id](const auto& e) { return e->id == id && !e->dead; });
}

std::size_t TweenSystem::active_count() const {
  return std::ranges::count_if(impl_->entries, [](const auto& e) { return !e->dead; });
}

void TweenSystem::update(float dt) {
  const std::size_t count = impl_->entries.size();  // tweens started now begin next update
  for (std::size_t i = 0; i < count; ++i) {
    Impl::Entry& e = *impl_->entries[i];
    if (e.dead) continue;
    if (e.scene != nullptr && !node_alive(e.scene, e.node)) { e.dead = true; continue; }
    e.elapsed += dt;
    bool all_done = true;
    for (std::size_t s = 0; s < e.steps.size() && !e.dead; ++s) {
      Impl::Step& step = e.steps[s];
      if (step.done) continue;
      const TweenValue v = evaluate_tween(step.spec, e.elapsed - step.offset);
      if (v.started) {
        if (step.apply) step.apply(v.value);
        step.done = v.finished;
      }
      if (!step.done) all_done = false;
    }
    if (all_done && !e.dead) {
      e.dead = true;
      if (e.on_complete) e.on_complete();
    }
  }
  std::erase_if(impl_->entries, [](const auto& e) { return e->dead; });
}

// --- Timers ----------------------------------------------------------------

struct Timers::Impl {
  struct Entry {
    TimerId id = 0;
    double interval = 0.0;
    double remaining = 0.0;
    bool repeat = false;
    std::function<void()> callback;
    Scene* scene = nullptr;
    NodeId node = kInvalidNodeId;
    bool dead = false;
  };
  std::vector<std::unique_ptr<Entry>> entries;
  TimerId next_id = 1;

  TimerId add(float seconds, bool repeat, std::function<void()> callback, Scene* scene, NodeId node) {
    auto e = std::make_unique<Entry>();
    e->id = next_id++;
    e->interval = std::max(0.0f, seconds);
    e->remaining = e->interval;
    e->repeat = repeat;
    e->callback = std::move(callback);
    e->scene = scene;
    e->node = node;
    const TimerId id = e->id;
    entries.push_back(std::move(e));
    return id;
  }
};

Timers::Timers() : impl_(std::make_unique<Impl>()) {}
Timers::~Timers() = default;
Timers::Timers(Timers&&) noexcept = default;
Timers& Timers::operator=(Timers&&) noexcept = default;

TimerId Timers::after(float seconds, std::function<void()> callback) {
  return impl_->add(seconds, false, std::move(callback), nullptr, kInvalidNodeId);
}
TimerId Timers::every(float seconds, std::function<void()> callback) {
  return impl_->add(seconds, true, std::move(callback), nullptr, kInvalidNodeId);
}
TimerId Timers::after_for_node(Scene& scene, NodeId node, float seconds, std::function<void()> callback) {
  return impl_->add(seconds, false, std::move(callback), &scene, node);
}
TimerId Timers::every_for_node(Scene& scene, NodeId node, float seconds, std::function<void()> callback) {
  return impl_->add(seconds, true, std::move(callback), &scene, node);
}

bool Timers::cancel(TimerId id) {
  for (auto& e : impl_->entries) {
    if (e->id == id && !e->dead) { e->dead = true; return true; }
  }
  return false;
}

std::size_t Timers::active_count() const {
  return std::ranges::count_if(impl_->entries, [](const auto& e) { return !e->dead; });
}

void Timers::update(float dt) {
  const std::size_t count = impl_->entries.size();
  for (std::size_t i = 0; i < count; ++i) {
    Impl::Entry& e = *impl_->entries[i];
    if (e.dead) continue;
    if (e.scene != nullptr && !node_alive(e.scene, e.node)) { e.dead = true; continue; }
    e.remaining -= dt;
    // A repeating timer with a large dt fires once per elapsed interval; an
    // interval of 0 fires once per update so it cannot loop forever.
    while (!e.dead && e.remaining <= kTimeEpsilon) {
      if (e.callback) e.callback();
      if (!e.repeat) { e.dead = true; break; }
      if (e.interval <= 0.0) break;
      e.remaining += e.interval;
    }
  }
  std::erase_if(impl_->entries, [](const auto& e) { return e->dead; });
}

// --- Tween component -------------------------------------------------------

void Tween::on_start() {
  if (autoplay) play();
}

void Tween::play() {
  const auto parsed = parse_easing(easing);
  if (!parsed || !is_node_tween_target(target) || node() == nullptr) {
    playing_ = false;
    return;
  }
  spec_ = {from, to, duration, delay, *parsed, loops, yoyo};
  if (use_current_from) {
    if (auto current = read_node_tween_target(*node(), target)) spec_.from = *current;
  }
  elapsed_ = 0.0;
  playing_ = true;
  finished_ = false;
}

void Tween::on_update(float fixed_dt) {
  if (!playing_) return;
  elapsed_ += fixed_dt;
  const TweenValue v = evaluate_tween(spec_, elapsed_);
  if (!v.started) return;
  if (!write_node_tween_target(*node(), target, v.value)) {
    playing_ = false;  // e.g. the Sprite was removed
    return;
  }
  if (v.finished) {
    playing_ = false;
    finished_ = true;
  }
}

void register_tween_components(ComponentRegistry& registry) {
  using T = Tween;
  registry.register_component(
      "Tween", [] { return std::make_unique<Tween>(); },
      {
          make_property<T>("target", PropertyType::String, &T::target),
          make_property<T>("from", PropertyType::Float, &T::from),
          make_property<T>("to", PropertyType::Float, &T::to),
          make_property<T>("use_current_from", PropertyType::Bool, &T::use_current_from),
          make_property<T>("duration", PropertyType::Float, &T::duration),
          make_property<T>("delay", PropertyType::Float, &T::delay),
          make_property<T>("easing", PropertyType::String, &T::easing),
          make_property<T>("loops", PropertyType::Int, &T::loops),
          make_property<T>("yoyo", PropertyType::Bool, &T::yoyo),
          make_property<T>("autoplay", PropertyType::Bool, &T::autoplay),
      });
}

}  // namespace arcane
