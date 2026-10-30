#include "arcane/replay.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <map>

#include "arcane/debug.hpp"
#include "arcane/physics.hpp"
#include "canonical_json.hpp"

namespace arcane {

namespace {

using nlohmann::json;

constexpr const char* kReplayFormat = "arcane.replay";
constexpr const char* kTraceFormat = "arcane.replay_trace";

const char* kind_name(ReplayEventKind kind) {
  switch (kind) {
    case ReplayEventKind::Key: return "key";
    case ReplayEventKind::MouseButton: return "mouse_button";
    case ReplayEventKind::PadButton: return "pad_button";
    case ReplayEventKind::PadAxis: return "pad_axis";
    case ReplayEventKind::MouseMove: return "mouse_move";
  }
  return "key";
}

std::optional<ReplayEventKind> kind_from_name(const std::string& name) {
  for (auto kind : {ReplayEventKind::Key, ReplayEventKind::MouseButton, ReplayEventKind::PadButton,
                    ReplayEventKind::PadAxis, ReplayEventKind::MouseMove}) {
    if (name == kind_name(kind)) return kind;
  }
  return std::nullopt;
}

std::expected<void, std::string> validate_recording(const ReplayRecording& r) {
  if (r.fixed_hz < 1 || r.fixed_hz > 1000) return std::unexpected(std::string("replay: fixed_hz must be 1..1000"));
  if (r.frames < 1 || r.frames > 1000000) return std::unexpected(std::string("replay: frames must be 1..1000000"));
  if (r.seed < 0) return std::unexpected(std::string("replay: seed must be >= 0"));
  int previous = 0;
  for (std::size_t i = 0; i < r.events.size(); ++i) {
    const ReplayEvent& e = r.events[i];
    const std::string where = std::format("replay event {}", i);
    if (e.frame < 0 || e.frame >= r.frames) return std::unexpected(where + ": frame is outside 0..frames-1");
    if (e.frame < previous) return std::unexpected(where + ": events must be ordered by frame");
    previous = e.frame;
    const bool pad = e.kind == ReplayEventKind::PadButton || e.kind == ReplayEventKind::PadAxis;
    if (pad && (e.pad < 0 || e.pad >= kMaxGamepads)) return std::unexpected(where + ": pad is out of range");
    if (e.kind == ReplayEventKind::PadAxis &&
        (!std::isfinite(e.value) || e.value < -1.0f || e.value > 1.0f || e.code < 0 || e.code >= GamepadAxes::kCount)) {
      return std::unexpected(where + ": axis code or value is out of range");
    }
    if (e.kind == ReplayEventKind::MouseMove && (!std::isfinite(e.position.x) || !std::isfinite(e.position.y))) {
      return std::unexpected(where + ": mouse position must be finite");
    }
  }
  return {};
}

// FNV-1a 64 over an explicit little-endian byte stream, so the value does not depend on host endianness.
class Hasher {
 public:
  void byte(unsigned char b) { state_ = (state_ ^ b) * 1099511628211ull; }
  void u64(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) byte(static_cast<unsigned char>(v >> (8 * i)));
  }
  void i32(int v) { u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(v))); }
  void f32(float v) { u64(v == 0.0f ? 0u : std::bit_cast<std::uint32_t>(v)); }
  void str(const std::string& s) {
    u64(s.size());
    for (char c : s) byte(static_cast<unsigned char>(c));
  }
  std::uint64_t value() const { return state_; }

 private:
  std::uint64_t state_ = 14695981039346656037ull;
};

void hash_value(Hasher& h, const PropertyValue& value) {
  h.u64(value.index());
  if (const auto* b = std::get_if<bool>(&value)) h.u64(*b ? 1 : 0);
  else if (const auto* i = std::get_if<int>(&value)) h.i32(*i);
  else if (const auto* f = std::get_if<float>(&value)) h.f32(*f);
  else if (const auto* v = std::get_if<Vec2>(&value)) { h.f32(v->x); h.f32(v->y); }
  else if (const auto* c = std::get_if<Color>(&value)) { h.f32(c->r); h.f32(c->g); h.f32(c->b); h.f32(c->a); }
  else if (const auto* s = std::get_if<std::string>(&value)) h.str(*s);
  else h.u64(std::get<AssetId>(value));
}

void hash_node(Hasher& h, const Node& node, const ComponentRegistry& registry) {
  h.u64(node.id());
  h.str(node.name());
  const Transform2D& t = node.local_transform();
  h.f32(t.position.x); h.f32(t.position.y); h.f32(t.rotation_radians); h.f32(t.scale.x); h.f32(t.scale.y);
  h.u64(node.pending_destroy() ? 1 : 0);
  h.u64(node.components().size());
  for (const auto& component : node.components()) {
    h.u64(component->id());
    h.str(component->type_name());
    const auto* properties = registry.properties_of(component->type_name());
    if (!properties) continue;
    for (const PropertyDescriptor& property : *properties) {
      h.str(property.name);
      hash_value(h, property.get(*component));
    }
  }
  h.u64(node.children().size());
  for (const auto& child : node.children()) hash_node(h, *child, registry);
}

void apply_seed(const std::vector<std::unique_ptr<Node>>& nodes, const ComponentRegistry& registry, int seed) {
  for (const auto& node : nodes) {
    for (const auto& component : node->components()) {
      const auto* properties = registry.properties_of(component->type_name());
      if (!properties) continue;
      for (const PropertyDescriptor& property : *properties) {
        if (property.name == "seed" && property.type == PropertyType::Int) property.set(*component, PropertyValue(seed));
      }
    }
    apply_seed(node->children(), registry, seed);
  }
}

void apply_event(FakeInputBackend& backend, const ReplayEvent& e) {
  switch (e.kind) {
    case ReplayEventKind::Key: backend.set_down(InputDevice::Keyboard, e.code, e.down); break;
    case ReplayEventKind::MouseButton: backend.set_down(InputDevice::MouseButton, e.code, e.down); break;
    case ReplayEventKind::PadButton: backend.set_gamepad_button(e.pad, e.code, e.down); break;
    case ReplayEventKind::PadAxis: backend.set_gamepad_axis(e.pad, e.code, e.value); break;
    case ReplayEventKind::MouseMove: backend.set_mouse_position(e.position); break;
  }
}

}  // namespace

std::string hash_to_hex(std::uint64_t hash) { return std::format("{:016x}", hash); }

std::expected<std::string, std::string> save_recording_json(const ReplayRecording& recording) {
  if (auto ok = validate_recording(recording); !ok) return std::unexpected(ok.error());
  json events = json::array();
  for (const ReplayEvent& e : recording.events) {
    json j = {{"frame", e.frame}, {"kind", kind_name(e.kind)}};
    switch (e.kind) {
      case ReplayEventKind::Key:
      case ReplayEventKind::MouseButton: j["code"] = e.code; j["down"] = e.down; break;
      case ReplayEventKind::PadButton: j["pad"] = e.pad; j["code"] = e.code; j["down"] = e.down; break;
      case ReplayEventKind::PadAxis: j["pad"] = e.pad; j["code"] = e.code; j["value"] = e.value; break;
      case ReplayEventKind::MouseMove: j["x"] = e.position.x; j["y"] = e.position.y; break;
    }
    events.push_back(std::move(j));
  }
  json root = {{"format", kReplayFormat}, {"version", 1}, {"fixed_hz", recording.fixed_hz},
               {"frames", recording.frames}, {"seed", recording.seed}, {"events", events}};
  return canonical::dump(root);
}

std::expected<ReplayRecording, std::string> load_recording_json(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("malformed replay JSON: {}", parsed.error()));
  try {
    const json& root = *parsed;
    canonical::require_object(root, "replay");
    canonical::require_only_keys(root, {"format", "version", "fixed_hz", "frames", "seed", "events"}, "replay");
    if (!root.contains("format") || root["format"] != kReplayFormat) canonical::fail("replay: 'format' must be \"arcane.replay\"");
    if (!root.contains("version") || !root["version"].is_number_integer() || root["version"] != 1) {
      canonical::fail("replay: unsupported file 'version' (this engine reads 1)");
    }
    if (!root.contains("frames")) canonical::fail("replay: 'frames' is required");
    ReplayRecording r;
    r.frames = canonical::as_int(root["frames"], "replay 'frames'");
    r.fixed_hz = canonical::as_int(root.value("fixed_hz", json(60)), "replay 'fixed_hz'");
    r.seed = canonical::as_int(root.value("seed", json(0)), "replay 'seed'");
    if (root.contains("events")) {
      if (!root["events"].is_array()) canonical::fail("replay: 'events' must be an array");
      std::size_t index = 0;
      for (const json& j : root["events"]) {
        const std::string where = std::format("replay event {}", index++);
        canonical::require_object(j, where);
        if (!j.contains("kind") || !j.contains("frame")) canonical::fail(where + ": 'frame' and 'kind' are required");
        const auto kind = kind_from_name(canonical::as_string(j["kind"], where + " 'kind'"));
        if (!kind) canonical::fail(where + ": unknown kind");
        ReplayEvent e;
        e.kind = *kind;
        e.frame = canonical::as_int(j["frame"], where + " 'frame'");
        const auto need = [&](const char* key) -> const json& {
          if (!j.contains(key)) canonical::fail(where + ": '" + key + "' is required");
          return j[key];
        };
        switch (e.kind) {
          case ReplayEventKind::Key:
          case ReplayEventKind::MouseButton:
            canonical::require_only_keys(j, {"frame", "kind", "code", "down"}, where);
            e.code = canonical::as_int(need("code"), where + " 'code'");
            e.down = canonical::as_bool(need("down"), where + " 'down'");
            break;
          case ReplayEventKind::PadButton:
            canonical::require_only_keys(j, {"frame", "kind", "pad", "code", "down"}, where);
            e.pad = canonical::as_int(need("pad"), where + " 'pad'");
            e.code = canonical::as_int(need("code"), where + " 'code'");
            e.down = canonical::as_bool(need("down"), where + " 'down'");
            break;
          case ReplayEventKind::PadAxis:
            canonical::require_only_keys(j, {"frame", "kind", "pad", "code", "value"}, where);
            e.pad = canonical::as_int(need("pad"), where + " 'pad'");
            e.code = canonical::as_int(need("code"), where + " 'code'");
            e.value = canonical::as_float(need("value"), where + " 'value'");
            break;
          case ReplayEventKind::MouseMove:
            canonical::require_only_keys(j, {"frame", "kind", "x", "y"}, where);
            e.position = {canonical::as_float(need("x"), where + " 'x'"), canonical::as_float(need("y"), where + " 'y'")};
            break;
        }
        r.events.push_back(e);
      }
    }
    if (auto ok = validate_recording(r); !ok) return std::unexpected(ok.error());
    return r;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::string(ex.what()));
  }
}

struct ReplayRecorder::Impl {
  struct Watch {
    ReplayEventKind kind;
    int pad;
    int code;
    bool down = false;
    float value = 0.0f;
  };
  ReplayRecording recording;
  std::vector<Watch> watches;
  Vec2 mouse{};
  int frame = 0;
};

ReplayRecorder::ReplayRecorder(int fixed_hz, int seed) : impl_(std::make_unique<Impl>()) {
  impl_->recording.fixed_hz = fixed_hz;
  impl_->recording.seed = seed;
}
ReplayRecorder::~ReplayRecorder() = default;
ReplayRecorder::ReplayRecorder(ReplayRecorder&&) noexcept = default;
ReplayRecorder& ReplayRecorder::operator=(ReplayRecorder&&) noexcept = default;

void ReplayRecorder::watch_key(int code) { impl_->watches.push_back({ReplayEventKind::Key, 0, code}); }
void ReplayRecorder::watch_mouse_button(int code) { impl_->watches.push_back({ReplayEventKind::MouseButton, 0, code}); }
void ReplayRecorder::watch_pad_button(int pad, int code) { impl_->watches.push_back({ReplayEventKind::PadButton, pad, code}); }
void ReplayRecorder::watch_pad_axes(int pad) {
  for (int axis = 0; axis < GamepadAxes::kCount; ++axis) impl_->watches.push_back({ReplayEventKind::PadAxis, pad, axis});
}

void ReplayRecorder::capture(const InputBackend& backend) {
  Impl& s = *impl_;
  for (Impl::Watch& w : s.watches) {
    ReplayEvent e;
    e.frame = s.frame;
    e.kind = w.kind;
    e.pad = w.pad;
    e.code = w.code;
    switch (w.kind) {
      case ReplayEventKind::Key:
      case ReplayEventKind::MouseButton: {
        const bool down = backend.is_down(w.kind == ReplayEventKind::Key ? InputDevice::Keyboard : InputDevice::MouseButton, w.code);
        if (down == w.down) continue;
        w.down = e.down = down;
        break;
      }
      case ReplayEventKind::PadButton: {
        const bool down = backend.gamepad_button(w.pad, w.code);
        if (down == w.down) continue;
        w.down = e.down = down;
        break;
      }
      default: {
        const float value = backend.gamepad_axis(w.pad, w.code);
        if (value == w.value) continue;
        w.value = e.value = value;
        break;
      }
    }
    s.recording.events.push_back(e);
  }
  const Vec2 mouse = backend.mouse_position();
  if (mouse.x != s.mouse.x || mouse.y != s.mouse.y) {
    s.mouse = mouse;
    ReplayEvent e;
    e.frame = s.frame;
    e.kind = ReplayEventKind::MouseMove;
    e.position = mouse;
    s.recording.events.push_back(e);
  }
  ++s.frame;
}

ReplayRecording ReplayRecorder::finish() const {
  ReplayRecording r = impl_->recording;
  r.frames = std::max(1, impl_->frame);
  return r;
}

const InputBackend& ReplayInput::advance() {
  while (next_event_ < recording_.events.size() && recording_.events[next_event_].frame <= frame_) {
    apply_event(backend_, recording_.events[next_event_++]);
  }
  ++frame_;
  return backend_;
}

void apply_replay_seed(Scene& scene, const ComponentRegistry& registry, int seed) {
  if (seed != 0) apply_seed(scene.roots(), registry, seed);
}

std::uint64_t hash_scene_state(const Scene& scene, const ComponentRegistry& registry) {
  Hasher h;
  h.u64(scene.roots().size());
  for (const auto& root : scene.roots()) hash_node(h, *root, registry);
  h.i32(count_particles(scene));
  return h.value();
}

std::expected<std::vector<std::uint64_t>, std::string> run_fixed_frames(Scene& scene, const ComponentRegistry& registry,
                                                                       InputActionMap& actions,
                                                                       const FixedRunOptions& options) {
  int frames = options.frames, fixed_hz = options.fixed_hz, seed = options.seed;
  if (options.recording) {
    if (auto ok = validate_recording(*options.recording); !ok) return std::unexpected(ok.error());
    frames = options.recording->frames;
    fixed_hz = options.recording->fixed_hz;
    seed = options.recording->seed;
  }
  if (fixed_hz < 1 || fixed_hz > 1000) return std::unexpected(std::string("run: fixed_hz must be 1..1000"));
  if (frames < 1 || frames > 1000000) return std::unexpected(std::string("run: frames must be 1..1000000"));
  if (seed < 0) return std::unexpected(std::string("run: seed must be >= 0"));
  apply_replay_seed(scene, registry, seed);
  const float dt = static_cast<float>(1.0 / fixed_hz);
  PhysicsWorld physics;
  physics.rebuild(scene);
  std::optional<ReplayInput> input;
  if (options.recording) input.emplace(*options.recording);
  FakeInputBackend idle;
  InputActionMap* previous = active_input_map();
  set_active_input_map(&actions);
  std::vector<std::uint64_t> hashes;
  hashes.reserve(static_cast<std::size_t>(frames));
  for (int frame = 0; frame < frames; ++frame) {
    actions.update(input ? input->advance() : idle);
    if (options.before_step) options.before_step(dt);
    scene.update(dt);
    physics.step(scene, dt);
    hashes.push_back(hash_scene_state(scene, registry));
  }
  set_active_input_map(previous);
  return hashes;
}

std::expected<std::vector<std::uint64_t>, std::string> run_replay(Scene& scene, const ComponentRegistry& registry,
                                                                 InputActionMap& actions,
                                                                 const ReplayRecording& recording) {
  FixedRunOptions options;
  options.recording = &recording;
  return run_fixed_frames(scene, registry, actions, options);
}

std::optional<int> first_divergent_frame(const std::vector<std::uint64_t>& a, const std::vector<std::uint64_t>& b) {
  const std::size_t common = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < common; ++i) {
    if (a[i] != b[i]) return static_cast<int>(i);
  }
  if (a.size() != b.size()) return static_cast<int>(common);
  return std::nullopt;
}

std::expected<std::string, std::string> save_trace_json(const std::vector<std::uint64_t>& hashes) {
  json list = json::array();
  for (std::uint64_t h : hashes) list.push_back(hash_to_hex(h));
  return canonical::dump(json{{"format", kTraceFormat}, {"version", 1}, {"hashes", list}});
}

std::expected<std::vector<std::uint64_t>, std::string> load_trace_json(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("malformed trace JSON: {}", parsed.error()));
  try {
    const json& root = *parsed;
    canonical::require_object(root, "trace");
    canonical::require_only_keys(root, {"format", "version", "hashes"}, "trace");
    if (!root.contains("format") || root["format"] != kTraceFormat) canonical::fail("trace: 'format' must be \"arcane.replay_trace\"");
    if (!root.contains("version") || !root["version"].is_number_integer() || root["version"] != 1) {
      canonical::fail("trace: unsupported file 'version' (this engine reads 1)");
    }
    if (!root.contains("hashes") || !root["hashes"].is_array()) canonical::fail("trace: 'hashes' must be an array");
    std::vector<std::uint64_t> hashes;
    for (const json& item : root["hashes"]) {
      const std::string s = canonical::as_string(item, "trace hash");
      if (s.size() != 16) canonical::fail("trace: hashes are 16 lowercase hex digits");
      std::uint64_t v = 0;
      for (char c : s) {
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) canonical::fail("trace: hashes are 16 lowercase hex digits");
        v = (v << 4) | static_cast<std::uint64_t>(d);
      }
      hashes.push_back(v);
    }
    return hashes;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::string(ex.what()));
  }
}

}  // namespace arcane
