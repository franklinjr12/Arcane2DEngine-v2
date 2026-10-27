#include "arcane/animation.hpp"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <format>

#include "arcane/component_registry.hpp"

namespace arcane {

namespace {
constexpr int kClipFormatVersion = 1;
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

std::string save_clip_to_json(const AnimationClip& clip) {
  nlohmann::json j;
  j["format_version"] = kClipFormatVersion;
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
  return j.dump(2);
}

std::expected<AnimationClip, std::string> load_clip_from_json(const std::string& text) {
  try {
    nlohmann::json j = nlohmann::json::parse(text);
    if (!j.contains("format_version") || j.at("format_version").get<int>() != kClipFormatVersion) {
      return std::unexpected("unsupported or missing format_version");
    }
    AnimationClip clip;
    clip.name = j.value("name", std::string());
    clip.loop = j.value("loop", true);
    for (const auto& f : j.at("frames")) {
      AnimationFrame frame;
      const auto& uv = f.at("uv");
      frame.uv = UvRect{uv.at(0).get<float>(), uv.at(1).get<float>(), uv.at(2).get<float>(), uv.at(3).get<float>()};
      frame.duration_seconds = f.value("duration_seconds", 0.1f);
      clip.frames.push_back(frame);
    }
    return clip;
  } catch (const nlohmann::json::exception& ex) {
    return std::unexpected(std::format("animation clip: {}", ex.what()));
  }
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
