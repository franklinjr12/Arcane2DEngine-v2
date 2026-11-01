#include "arcane/material.hpp"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

#include "arcane/atlas.hpp"
#include "arcane/canonical_json.hpp"
#include "canonical_json.hpp"

namespace arcane {

namespace {

constexpr const char* kMaterialFormat = "arcane.material";

const char* blend_name(MaterialBlend b) {
  switch (b) {
    case MaterialBlend::Alpha: return "alpha";
    case MaterialBlend::Additive: return "additive";
    case MaterialBlend::Opaque: return "opaque";
  }
  return "alpha";
}

bool is_glsl_identifier(const std::string& s) {
  if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0]))) return false;
  return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}

// Empty string if fine.
std::string check_param(const MaterialParam& p) {
  if (!is_glsl_identifier(p.name)) return std::format("param '{}' is not a GLSL identifier", p.name);
  if (p.name.rfind("u_", 0) == 0 || p.name.rfind("gl_", 0) == 0) {
    return std::format("param '{}' uses a reserved prefix (u_ or gl_)", p.name);
  }
  if (p.value.empty() || p.value.size() > 4) return std::format("param '{}' needs 1 to 4 floats", p.name);
  for (float v : p.value) {
    if (!std::isfinite(v)) return std::format("param '{}' has a non-finite value", p.name);
  }
  return {};
}

std::string check_shader(const std::string& shader) {
  if (!is_project_relative_path(shader)) return std::format("shader '{}' is not a project-relative path", shader);
  if (shader.size() < 6 || shader.compare(shader.size() - 5, 5, ".frag") != 0) {
    return std::format("shader '{}' must have the .frag extension", shader);
  }
  return {};
}

}  // namespace

std::expected<std::string, std::string> save_material_canonical_json(const Material& material) {
  if (auto m = check_shader(material.shader); !m.empty()) return std::unexpected("material: " + m);
  std::vector<MaterialParam> params = material.params;
  std::sort(params.begin(), params.end(), [](const MaterialParam& a, const MaterialParam& b) { return a.name < b.name; });
  std::set<std::string> names;
  for (const auto& p : params) {
    if (auto m = check_param(p); !m.empty()) return std::unexpected("material: " + m);
    if (!names.insert(p.name).second) return std::unexpected(std::format("material: duplicate param '{}'", p.name));
  }
  nlohmann::json j;
  j["format"] = kMaterialFormat;
  j["version"] = 2;
  j["shader"] = material.shader;
  j["blend"] = blend_name(material.blend);
  nlohmann::json list = nlohmann::json::array();
  for (const auto& p : params) {
    nlohmann::json value = nlohmann::json::array();
    for (float v : p.value) value.push_back(v);
    list.push_back({{"name", p.name}, {"value", std::move(value)}});
  }
  j["params"] = std::move(list);
  return canonical::dump(j);
}

std::expected<Material, std::string> load_material_from_json(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("material: {}", parsed.error()));
  try {
    const nlohmann::json& j = *parsed;
    canonical::require_object(j, "material");
    if (canonical::detect_dialect(j, kMaterialFormat, "material") != canonical::Dialect::V2) {
      return std::unexpected(std::string("material: needs format 'arcane.material' version 2"));
    }
    canonical::require_only_keys(j, {"format", "version", "shader", "blend", "params"}, "material");
    if (!j.contains("shader")) return std::unexpected(std::string("material: missing 'shader'"));
    Material material;
    material.shader = canonical::as_string(j["shader"], "material 'shader'");
    if (auto m = check_shader(material.shader); !m.empty()) return std::unexpected("material: " + m);
    if (j.contains("blend")) {
      const std::string blend = canonical::as_string(j["blend"], "material 'blend'");
      if (blend == "alpha") material.blend = MaterialBlend::Alpha;
      else if (blend == "additive") material.blend = MaterialBlend::Additive;
      else if (blend == "opaque") material.blend = MaterialBlend::Opaque;
      else return std::unexpected(std::format("material: unknown blend '{}'", blend));
    }
    if (j.contains("params")) {
      if (!j["params"].is_array()) return std::unexpected(std::string("material: 'params' must be an array"));
      std::set<std::string> names;
      for (std::size_t i = 0; i < j["params"].size(); ++i) {
        const auto& p = j["params"][i];
        const std::string where = std::format("material param {}", i);
        canonical::require_object(p, where);
        canonical::require_only_keys(p, {"name", "value"}, where);
        for (const char* key : {"name", "value"}) {
          if (!p.contains(key)) return std::unexpected(std::format("material: {} is missing '{}'", where, key));
        }
        MaterialParam param;
        param.name = canonical::as_string(p["name"], where);
        if (!p["value"].is_array()) return std::unexpected(std::format("material: {} 'value' must be an array", where));
        for (const auto& v : p["value"]) param.value.push_back(canonical::as_float(v, where));
        if (auto m = check_param(param); !m.empty()) return std::unexpected("material: " + m);
        if (!names.insert(param.name).second) return std::unexpected(std::format("material: duplicate param '{}'", param.name));
        material.params.push_back(std::move(param));
      }
      std::sort(material.params.begin(), material.params.end(),
                [](const MaterialParam& a, const MaterialParam& b) { return a.name < b.name; });
    }
    return material;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::format("material: {}", ex.what()));
  }
}

}  // namespace arcane
