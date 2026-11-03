#include "arcane/animation.hpp"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <format>

#include "arcane/component_registry.hpp"
#include "canonical_json.hpp"

namespace arcane {

namespace {
constexpr const char* kClipFormat = "arcane.clip";
}  // namespace

float clip_duration(const AnimationClip& clip) noexcept {
  float total = 0.0f;
  for (const auto& frame : clip.frames) {
    total += frame.duration_seconds;
  }
  return total;
}

namespace {

// Playback timeline of a clip: `count` virtual frames over `period` seconds.
// Ping-pong lists the frames forward then backward (0..n-1, n-1..0), so the
// end frames are held twice as long, like a folded timeline.
struct Timeline {
  std::size_t frames;
  std::size_t count;
  bool ping_pong;
  double period;
  std::size_t frame_at(std::size_t j) const { return j < frames ? j : 2 * frames - 1 - j; }
};

Timeline make_timeline(const AnimationClip& clip) {
  Timeline tl{clip.frames.size(), clip.frames.size(), clip.loop && clip.ping_pong && clip.frames.size() > 1,
              static_cast<double>(clip_duration(clip))};
  if (tl.ping_pong) {
    tl.count *= 2;
    tl.period *= 2.0;
  }
  return tl;
}

}  // namespace

std::size_t select_frame_index(const AnimationClip& clip, float elapsed_seconds) noexcept {
  if (clip.frames.empty()) {
    return 0;
  }
  const float total = clip_duration(clip);
  if (!(total > 0.0f) || !std::isfinite(elapsed_seconds)) {
    return 0;
  }

  const Timeline tl = make_timeline(clip);
  double t = elapsed_seconds;
  if (clip.loop) {
    t = std::fmod(t, tl.period);
    if (t < 0.0) t += tl.period;
  } else {
    t = std::clamp(t, 0.0, tl.period);
  }

  double accumulated = 0.0;
  for (std::size_t j = 0; j < tl.count; ++j) {
    const std::size_t i = tl.frame_at(j);
    accumulated += clip.frames[i].duration_seconds;
    if (t < accumulated) {
      return i;
    }
  }
  return clip.frames.size() - 1;
}

bool clip_finished(const AnimationClip& clip, float elapsed_seconds) noexcept {
  if (clip.loop || clip.frames.empty()) return false;
  const float total = clip_duration(clip);
  return elapsed_seconds >= total || elapsed_seconds <= 0.0f;
}

std::vector<std::size_t> frame_events(const AnimationClip& clip, float from_seconds, float to_seconds) {
  std::vector<std::size_t> events;
  if (clip.frames.empty() || !std::isfinite(from_seconds) || !std::isfinite(to_seconds) ||
      from_seconds == to_seconds) {
    return events;
  }
  const Timeline tl = make_timeline(clip);
  if (!(tl.period > 0.0)) return events;

  constexpr double kMaxPeriods = 64.0;
  double from = from_seconds;
  double to = to_seconds;
  const bool forward = to > from;
  if (!clip.loop) {
    from = std::clamp(from, 0.0, tl.period);
    to = std::clamp(to, 0.0, tl.period);
  } else if (std::abs(to - from) > kMaxPeriods * tl.period) {
    // Keep only the part of the move nearest the destination.
    from = forward ? to - kMaxPeriods * tl.period : to + kMaxPeriods * tl.period;
  }
  const double lo = std::min(from, to);
  const double hi = std::max(from, to);

  // Start time of each virtual frame within one period.
  std::vector<double> starts(tl.count);
  double acc = 0.0;
  for (std::size_t j = 0; j < tl.count; ++j) {
    starts[j] = acc;
    acc += clip.frames[tl.frame_at(j)].duration_seconds;
  }

  struct Hit {
    double time;
    std::size_t frame;
  };
  std::vector<Hit> hits;
  const long first = clip.loop ? static_cast<long>(std::floor(lo / tl.period)) : 0;
  const long last = clip.loop ? static_cast<long>(std::floor(hi / tl.period)) : 0;
  for (long k = first; k <= last; ++k) {
    for (std::size_t j = 0; j < tl.count; ++j) {
      // Boundary j starts virtual frame j; the first one of a non-looping
      // clip is the clip start, not a crossing.
      if (!clip.loop && j == 0) continue;
      if (tl.ping_pong && (j == 0 || j == tl.frames)) continue;  // turn-arounds repeat a frame
      const double b = static_cast<double>(k) * tl.period + starts[j];
      if (forward ? (b > from && b <= to) : (b > to && b <= from)) {
        hits.push_back({b, forward ? tl.frame_at(j) : tl.frame_at(j == 0 ? tl.count - 1 : j - 1)});
      }
    }
  }
  std::sort(hits.begin(), hits.end(), [forward](const Hit& a, const Hit& b) {
    return forward ? a.time < b.time : a.time > b.time;
  });
  for (const Hit& h : hits) events.push_back(h.frame);
  return events;
}

std::expected<std::string, std::string> save_clip_canonical_json(const AnimationClip& clip) {
  nlohmann::json j;
  j["format"] = kClipFormat;
  j["version"] = 2;
  j["name"] = clip.name;
  j["loop"] = clip.loop;
  nlohmann::json frames = nlohmann::json::array();
  for (const auto& frame : clip.frames) {
    nlohmann::json f;
    f["uv"] = nlohmann::json::array({frame.uv.u0, frame.uv.v0, frame.uv.u1, frame.uv.v1});
    f["duration_seconds"] = frame.duration_seconds;
    frames.push_back(std::move(f));
  }
  j["frames"] = std::move(frames);
  return canonical::dump(j);
}

std::string save_clip_to_json(const AnimationClip& clip) {
  return save_clip_canonical_json(clip).value_or(std::string());
}

namespace {

std::expected<AnimationClip, std::string> load_clip(const std::string& text, bool strict_unknown) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("animation clip: {}", parsed.error()));
  try {
    const nlohmann::json& j = *parsed;
    canonical::require_object(j, "animation clip");
    const canonical::Dialect dialect = canonical::detect_dialect(j, kClipFormat, "animation clip");
    const bool strict = strict_unknown || dialect == canonical::Dialect::V2;
    if (strict) {
      if (dialect == canonical::Dialect::V2) {
        canonical::require_only_keys(j, {"format", "version", "name", "loop", "frames"}, "animation clip");
      } else {
        canonical::require_only_keys(j, {"format_version", "name", "loop", "frames"}, "animation clip");
      }
    }
    if (!j.contains("frames") || !j["frames"].is_array()) {
      return std::unexpected(std::string("animation clip: missing 'frames' array"));
    }
    AnimationClip clip;
    if (j.contains("name")) clip.name = canonical::as_string(j["name"], "clip 'name'");
    if (j.contains("loop")) clip.loop = canonical::as_bool(j["loop"], "clip 'loop'");
    for (std::size_t i = 0; i < j["frames"].size(); ++i) {
      const auto& f = j["frames"][i];
      const std::string where = std::format("clip frame {}", i);
      canonical::require_object(f, where);
      if (strict) canonical::require_only_keys(f, {"uv", "duration_seconds"}, where);
      if (!f.contains("uv") || !f["uv"].is_array() || f["uv"].size() != 4) {
        return std::unexpected(std::format("animation clip: {} needs a 'uv' array of 4 numbers", where));
      }
      AnimationFrame frame;
      frame.uv = UvRect{canonical::as_float(f["uv"][0], where), canonical::as_float(f["uv"][1], where),
                        canonical::as_float(f["uv"][2], where), canonical::as_float(f["uv"][3], where)};
      if (f.contains("duration_seconds")) {
        frame.duration_seconds = canonical::as_float(f["duration_seconds"], where + " 'duration_seconds'");
      }
      if (strict && frame.duration_seconds < 0.0f) {
        return std::unexpected(std::format("animation clip: {} has a negative duration", where));
      }
      clip.frames.push_back(frame);
    }
    return clip;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::format("animation clip: {}", ex.what()));
  }
}

}  // namespace

std::expected<AnimationClip, std::string> load_clip_from_json(const std::string& text) {
  return load_clip(text, false);
}

std::expected<AnimationClip, std::string> load_clip_from_json_strict(const std::string& text) {
  return load_clip(text, true);
}

std::expected<std::string, std::string> migrate_clip_json(const std::string& text) {
  auto clip = load_clip_from_json_strict(text);
  if (!clip) return std::unexpected(clip.error());
  auto migrated = save_clip_canonical_json(*clip);
  if (!migrated) return std::unexpected(migrated.error());
  auto reloaded = load_clip_from_json_strict(*migrated);
  if (!reloaded) return std::unexpected("migrated clip does not reload: " + reloaded.error());
  auto again = save_clip_canonical_json(*reloaded);
  if (!again || *again != *migrated) return std::unexpected(std::string("migrated clip is not idempotent"));
  return migrated;
}

void AnimatedSprite::on_update(float fixed_dt) {
  previous_elapsed_seconds_ = elapsed_seconds_;
  if (!playing) return;
  const float advance = fixed_dt * speed;
  if (std::isfinite(advance)) elapsed_seconds_ += advance;
}

void register_animation_components(ComponentRegistry& registry) {
  registry.register_component(
      "AnimatedSprite", [] { return std::make_unique<AnimatedSprite>(); },
      {
          make_property<AnimatedSprite>("clip", PropertyType::AssetId, &AnimatedSprite::clip),
          make_property<AnimatedSprite>("speed", PropertyType::Float, &AnimatedSprite::speed),
          make_property<AnimatedSprite>("playing", PropertyType::Bool, &AnimatedSprite::playing),
      });
}

}  // namespace arcane
