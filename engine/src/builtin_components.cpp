#include "arcane/builtin_components.hpp"

#include <memory>

#include "arcane/component_registry.hpp"

namespace arcane {

void register_builtin_components(ComponentRegistry& registry) {
  registry.register_component(
      "Sprite", [] { return std::make_unique<Sprite>(); },
      {
          make_property<Sprite>("texture", PropertyType::AssetId, &Sprite::texture),
          make_property<Sprite>("width", PropertyType::Float, &Sprite::width),
          make_property<Sprite>("height", PropertyType::Float, &Sprite::height),
          make_property<Sprite>("tint", PropertyType::Color, &Sprite::tint),
      });
  registry.register_component(
      "Camera", [] { return std::make_unique<Camera>(); },
      {
          make_property<Camera>("zoom", PropertyType::Float, &Camera::zoom),
          make_property<Camera>("primary", PropertyType::Bool, &Camera::primary),
      });
}

}  // namespace arcane
