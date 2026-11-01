#pragma once

#include <expected>
#include <string>
#include <vector>

#include "arcane/export.hpp"

namespace arcane {

enum class MaterialBlend { Alpha, Additive, Opaque };

// One user uniform: 1 to 4 floats, declared as float/vec2/vec3/vec4 by count.
struct MaterialParam {
  std::string name;
  std::vector<float> value;
  bool operator==(const MaterialParam&) const = default;
};

// Canonical `arcane.material` v2 (docs/formats/material.md): a GLSL fragment
// shader file plus blend mode and uniform values.
struct Material {
  std::string shader;  // project-relative path of a .frag file (fragment body only)
  MaterialBlend blend = MaterialBlend::Alpha;
  std::vector<MaterialParam> params;  // sorted by name, unique
  bool operator==(const Material&) const = default;
};

// Writes params sorted by name. Fails for invalid names/values.
ARCANE_API std::expected<std::string, std::string> save_material_canonical_json(const Material& material);
ARCANE_API std::expected<Material, std::string> load_material_from_json(const std::string& text);

}  // namespace arcane
