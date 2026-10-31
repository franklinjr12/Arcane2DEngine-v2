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

std::size_t select_frame_index(const AnimationClip& clip, float elapsed_seconds) noexcept {
  if (clip.frames.empty()) {
    return 0;
  }
  const float total = clip_duration(clip);
  if (total <= 0.0f) {
    return 0;
  }

  float t = elapsed_seconds;
  if (clip.loop) {
    t = std::fmod(t, total);
    if (t < 0.0f) t += total;
  } else {
    t = std::clamp(t, 0.0f, total);
  }

  float accumulated = 0.0f;
  for (std::size_t i = 0; i < clip.frames.size(); ++i) {
    accumulated += clip.frames[i].duration_seconds;
    if (t < accumulated) {
      return i;
    }
  }
  return clip.frames.size() - 1;
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
  if (!parsed) return std::unexpected(canonical::at_root("animation clip: " + parsed.error()));
  try {
    const nlohmann::json& j = *parsed;
    canonical::require_object(j, "#");
    const canonical::Dialect dialect = canonical::detect_dialect(j, kClipFormat, "animation clip");
    const bool strict = strict_unknown || dialect == canonical::Dialect::V2;
    if (strict) {
      if (dialect == canonical::Dialect::V2) {
        canonical::require_only_keys(j, {"format", "version", "name", "loop", "frames"}, "#");
      } else {
        canonical::require_only_keys(j, {"format_version", "name", "loop", "frames"}, "#");
      }
    }
    if (!j.contains("frames") || !j["frames"].is_array()) {
      return std::unexpected(std::string("#/frames: animation clip is missing a 'frames' array"));
    }
    AnimationClip clip;
    if (j.contains("name")) clip.name = canonical::as_string(j["name"], "#/name");
    if (j.contains("loop")) clip.loop = canonical::as_bool(j["loop"], "#/loop");
    for (std::size_t i = 0; i < j["frames"].size(); ++i) {
      const auto& f = j["frames"][i];
      const std::string where = canonical::pointer_child("#/frames", i);
      canonical::require_object(f, where);
      if (strict) canonical::require_only_keys(f, {"uv", "duration_seconds"}, where);
      if (!f.contains("uv") || !f["uv"].is_array() || f["uv"].size() != 4) {
        return std::unexpected(std::format("{}: animation clip frame needs a 'uv' array of 4 numbers", canonical::pointer_child(where, "uv")));
      }
      AnimationFrame frame;
      const std::string uv = canonical::pointer_child(where, "uv");
      frame.uv = UvRect{canonical::as_float(f["uv"][0], canonical::pointer_child(uv, 0)),
                        canonical::as_float(f["uv"][1], canonical::pointer_child(uv, 1)),
                        canonical::as_float(f["uv"][2], canonical::pointer_child(uv, 2)),
                        canonical::as_float(f["uv"][3], canonical::pointer_child(uv, 3))};
      if (f.contains("duration_seconds")) {
        frame.duration_seconds = canonical::as_float(f["duration_seconds"], canonical::pointer_child(where, "duration_seconds"));
      }
      if (strict && frame.duration_seconds < 0.0f) {
        return std::unexpected(std::format("{}: animation clip frame has a negative duration", canonical::pointer_child(where, "duration_seconds")));
      }
      clip.frames.push_back(frame);
    }
    return clip;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::string(ex.what()));
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
  if (!reloaded) return std::unexpected("#: migrated clip does not reload: " + reloaded.error());
  auto again = save_clip_canonical_json(*reloaded);
  if (!again || *again != *migrated) return std::unexpected(std::string("#: migrated clip is not idempotent"));
  return migrated;
}

void AnimatedSprite::on_update(float fixed_dt) {
  if (playing) {
    elapsed_seconds_ += fixed_dt * speed;
  }
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
