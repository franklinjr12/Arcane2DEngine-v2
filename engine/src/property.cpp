#include "arcane/property.hpp"

namespace arcane {

std::string_view to_string(PropertyType type) noexcept {
  switch (type) {
    case PropertyType::Bool:
      return "bool";
    case PropertyType::Int:
      return "int";
    case PropertyType::Float:
      return "float";
    case PropertyType::Vec2:
      return "vec2";
    case PropertyType::Color:
      return "color";
    case PropertyType::String:
      return "string";
    case PropertyType::AssetId:
      return "asset_id";
    case PropertyType::EntityId:
      return "entity_id";
  }
  return "unknown";
}

}  // namespace arcane
