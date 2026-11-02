// AF12: input recording/replay, canonical state hash and divergence reporting, plus replay.* commands.
// Headless: no window, GL or audio.
#include <filesystem>
#include <fstream>
#include <sstream>

#include "arcane/builtin_components.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/particles.hpp"
#include "arcane/physics.hpp"
#include "arcane/replay.hpp"
#include "arcane_editor/project_session.hpp"
#include "arcane_tooling/command.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;
using namespace arcane::tooling;
namespace fs = std::filesystem;

// Moves its node right while the "right" action is held.
class Mover : public Component {
 public:
  float speed = 60.0f;
  void on_update(float dt) override {
    InputActionMap* map = active_input_map();
    if (map && map->held("right")) node()->local_transform().position.x += speed * dt;
  }
};

void register_all(ComponentRegistry& registry) {
  register_builtin_components(registry);
  register_physics_components(registry);
  register_particle_components(registry);
  registry.register_component("Mover", [] { return std::make_unique<Mover>(); },
                              {make_property<Mover, float>("speed", PropertyType::Float, &Mover::speed)});
}

InputActionMap make_actions() {
  InputActionMap actions;
  actions.set_action({"right", {InputBinding{InputDevice::Keyboard, Keys::kD}}, -1});
  return actions;
}

void build_scene(Scene& scene, const ComponentRegistry& registry) {
  Node* hero = scene.create_node("hero");
  scene.add_component(hero, "Mover", registry);
  Node* ball = scene.create_node("ball");
  ball->local_transform().position = {0.0f, -50.0f};
  scene.add_component(ball, "RigidBody2D", registry);
  scene.add_component(ball, "Collider2D", registry);
  Node* emitter = scene.create_node("sparks");
  auto* e = static_cast<ParticleEmitter*>(scene.add_component(emitter, "ParticleEmitter", registry));
  e->rate = 30.0f;
}

ReplayRecording make_recording() {
  ReplayRecording r;
  r.frames = 90;
  r.seed = 7;
  r.events = {{.frame = 10, .kind = ReplayEventKind::Key, .code = Keys::kD, .down = true},
              {.frame = 40, .kind = ReplayEventKind::Key, .code = Keys::kD, .down = false},
              {.frame = 50, .kind = ReplayEventKind::PadAxis, .pad = 1, .code = 0, .value = 0.5f},
              {.frame = 60, .kind = ReplayEventKind::MouseMove, .position = {12.5f, 3.0f}}};
  return r;
}

std::vector<std::uint64_t> replay(const ReplayRecording& recording) {
  ComponentRegistry registry;
  register_all(registry);
  Scene scene;
  build_scene(scene, registry);
  InputActionMap actions = make_actions();
  auto hashes = run_replay(scene, registry, actions, recording);
  ARCANE_ASSERT_TRUE(hashes.has_value());
  return *hashes;
}

void test_format() {
  const ReplayRecording r = make_recording();
  auto text = save_recording_json(r);
  ARCANE_ASSERT_TRUE(text.has_value());
  ARCANE_ASSERT_TRUE(canonicalize_json(*text).value() == *text);
  auto back = load_recording_json(*text);
  ARCANE_ASSERT_TRUE(back.has_value() && *back == r);

  const auto bad = [](const std::string& s) { return !load_recording_json(s).has_value(); };
  ARCANE_ASSERT_TRUE(bad("{\"format\":\"arcane.replay\",\"version\":2,\"frames\":1}"));
  ARCANE_ASSERT_TRUE(bad("{\"format\":\"arcane.replay\",\"version\":1,\"frames\":0}"));
  ARCANE_ASSERT_TRUE(bad("{\"format\":\"arcane.replay\",\"version\":1,\"frames\":1,\"extra\":1}"));
  ARCANE_ASSERT_TRUE(bad("{\"format\":\"arcane.replay\",\"version\":1,\"frames\":5,\"events\":"
                         "[{\"frame\":5,\"kind\":\"key\",\"code\":1,\"down\":true}]}"));  // frame outside range
  ARCANE_ASSERT_TRUE(bad("{\"format\":\"arcane.replay\",\"version\":1,\"frames\":5,\"events\":"
                         "[{\"frame\":1,\"kind\":\"key\",\"code\":1}]}"));  // missing down
  ARCANE_ASSERT_TRUE(bad("{\"format\":\"arcane.replay\",\"version\":1,\"frames\":5,\"events\":"
                         "[{\"frame\":3,\"kind\":\"key\",\"code\":1,\"down\":true},"
                         "{\"frame\":2,\"kind\":\"key\",\"code\":1,\"down\":false}]}"));  // unordered

  std::vector<std::uint64_t> hashes{1, 0xdeadbeefcafef00dull};
  auto trace = save_trace_json(hashes);
  ARCANE_ASSERT_TRUE(trace.has_value() && load_trace_json(*trace).value() == hashes);
  ARCANE_ASSERT_TRUE(!load_trace_json("{\"format\":\"arcane.replay_trace\",\"version\":1,\"hashes\":[\"xyz\"]}").has_value());
}

void test_determinism_and_divergence() {
  const ReplayRecording r = make_recording();
  const auto a = replay(r);
  const auto b = replay(r);
  ARCANE_ASSERT_TRUE(a.size() == 90 && a == b);
  ARCANE_ASSERT_TRUE(!first_divergent_frame(a, b).has_value());

  // Input affects state: before frame 10 the key is up, so nothing differs there.
  ReplayRecording changed = r;
  changed.events[0].frame = 20;  // press D ten frames later
  const auto c = replay(changed);
  const auto frame = first_divergent_frame(a, c);
  ARCANE_ASSERT_TRUE(frame && *frame == 10);

  ReplayRecording reseeded = r;
  reseeded.seed = 8;  // different particle stream
  ARCANE_ASSERT_TRUE(first_divergent_frame(a, replay(reseeded)).has_value());

  std::vector<std::uint64_t> shorter(a.begin(), a.begin() + 30);
  ARCANE_ASSERT_TRUE(first_divergent_frame(a, shorter) == 30);
}

void test_recorder() {
  FakeInputBackend backend;
  ReplayRecorder recorder(60, 7);
  recorder.watch_key(Keys::kD);
  recorder.watch_pad_axes(0);
  recorder.capture(backend);                          // frame 0: nothing
  backend.set_down(InputDevice::Keyboard, Keys::kD, true);
  backend.set_gamepad_axis(0, GamepadAxes::kLeftX, 0.25f);
  backend.set_mouse_position({4.0f, 5.0f});
  recorder.capture(backend);                          // frame 1
  recorder.capture(backend);                          // frame 2: no change, no events
  const ReplayRecording r = recorder.finish();
  ARCANE_ASSERT_TRUE(r.frames == 3 && r.seed == 7 && r.events.size() == 3);
  ARCANE_ASSERT_TRUE(r.events[0].frame == 1 && r.events[0].kind == ReplayEventKind::Key && r.events[0].down);
  ARCANE_ASSERT_TRUE(save_recording_json(r).has_value());
}

void test_commands() {
  const fs::path dir = fs::temp_directory_path() / "arcane_replay_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  ComponentRegistry components;
  register_builtin_components(components);
  auto session = editor::ProjectSession::create(dir, "Replay", components);
  ARCANE_ASSERT_TRUE(session.has_value());
  session->scene().create_node("thing");
  ARCANE_ASSERT_TRUE(session->save(components).has_value());

  CommandRegistry registry;
  register_replay_commands(registry);
  auto context = ProjectContext::open(dir);
  ARCANE_ASSERT_TRUE(context.has_value());
  ProjectContext& ctx = **context;

  auto saved = registry.execute(ctx, {"replay.save", {{"file", "replays/a.replay.json"}, {"frames", 20},
                {"events", Json::array({{{"frame", 2}, {"kind", "key"}, {"code", 68}, {"down", true}}})}}});
  ARCANE_ASSERT_TRUE(saved.has_value() && fs::exists(dir / "replays/a.replay.json"));
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"replay.save", {{"file", "../x.json"}, {"frames", 2}}}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"replay.save", {{"file", "b.json"}, {"frames", 2},
                {"events", Json::array({{{"frame", 9}, {"kind", "key"}, {"code", 68}, {"down", true}}})}}}).has_value());

  auto run = registry.execute(ctx, {"replay.run", {{"recording", "replays/a.replay.json"}, {"trace_out", "replays/a.trace.json"}}});
  ARCANE_ASSERT_TRUE(run.has_value() && run->value["frames"] == 20 && run->value["hashes"].size() == 20);
  auto again = registry.execute(ctx, {"replay.run", {{"recording", "replays/a.replay.json"}, {"expected", "replays/a.trace.json"}}});
  ARCANE_ASSERT_TRUE(again.has_value() && again->value["final_hash"] == run->value["final_hash"]);

  // Tamper with frame 5 of the trace: the command reports exactly that frame.
  auto text = read_text_file(dir / "replays/a.trace.json");
  auto trace = load_trace_json(*text);
  (*trace)[5] ^= 1;
  ARCANE_ASSERT_TRUE(write_text_file_atomic(dir / "replays/bad.trace.json", save_trace_json(*trace).value()).has_value());
  auto diverged = registry.execute(ctx, {"replay.run", {{"recording", "replays/a.replay.json"}, {"expected", "replays/bad.trace.json"}}});
  ARCANE_ASSERT_TRUE(!diverged.has_value() && diverged.error().code == "REPLAY_DIVERGED");
  ARCANE_ASSERT_TRUE(diverged.error().message.find("frame is 5") != std::string::npos);
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"replay.run", {{"recording", "missing.json"}}}).has_value());
  fs::remove_all(dir, ec);
}

}  // namespace

void test_fixed_frames() {
  ComponentRegistry registry;
  register_all(registry);
  const auto run = [&](const FixedRunOptions& o) {
    Scene scene;
    build_scene(scene, registry);
    InputActionMap actions = make_actions();
    return run_fixed_frames(scene, registry, actions, o);
  };
  FixedRunOptions plain;
  plain.frames = 30;
  plain.fixed_hz = 30;
  int calls = 0;
  bool dt_ok = true;
  plain.before_step = [&](float dt) { ++calls; dt_ok = dt_ok && dt == static_cast<float>(1.0 / 30); };
  auto a = run(plain);
  auto b = run(plain);
  ARCANE_ASSERT_TRUE(a.has_value() && b.has_value() && a->size() == 30 && *a == *b);
  ARCANE_ASSERT_TRUE(calls == 60 && dt_ok);

  const ReplayRecording rec = make_recording();
  FixedRunOptions with_rec;
  with_rec.recording = &rec;
  auto c = run(with_rec);
  ARCANE_ASSERT_TRUE(c.has_value() && *c == replay(rec));

  FixedRunOptions bad;
  bad.frames = 0;
  ARCANE_ASSERT_TRUE(!run(bad).has_value());
  bad.frames = 1000001;
  ARCANE_ASSERT_TRUE(!run(bad).has_value());
  bad.frames = 1;
  bad.fixed_hz = 1001;
  ARCANE_ASSERT_TRUE(!run(bad).has_value());
  bad.fixed_hz = 0;
  ARCANE_ASSERT_TRUE(!run(bad).has_value());
}

int main() {
  test_format();
  test_determinism_and_divergence();
  test_recorder();
  test_commands();
  test_fixed_frames();
  std::puts("test_replay OK");
  return 0;
}
