#include "arcane/property.hpp"

#include <algorithm>

#include "arcane/component_registry.hpp"

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

bool value_fits_type(PropertyType type, const PropertyValue& value) noexcept {
  switch (type) {
    case PropertyType::Bool:
      return std::holds_alternative<bool>(value);
    case PropertyType::Int:
      return std::holds_alternative<int>(value);
    case PropertyType::Float:
      return std::holds_alternative<float>(value);
    case PropertyType::Vec2:
      return std::holds_alternative<Vec2>(value);
    case PropertyType::Color:
      return std::holds_alternative<Color>(value);
    case PropertyType::String:
      return std::holds_alternative<std::string>(value);
    case PropertyType::AssetId:
    case PropertyType::EntityId:
      return std::holds_alternative<AssetId>(value);
  }
  return false;
}

std::vector<ComponentInfo> describe_components(const ComponentRegistry& registry) {
  std::vector<std::string> names = registry.type_names();
  std::sort(names.begin(), names.end());
  std::vector<ComponentInfo> result;
  result.reserve(names.size());
  for (const std::string& name : names) {
    ComponentInfo info{name, {}};
    const auto* descriptors = registry.properties_of(name);
    const std::unique_ptr<Component> fresh = registry.create(name);
    if (descriptors != nullptr && fresh != nullptr) {
      for (const PropertyDescriptor& d : *descriptors) {
        info.properties.push_back(PropertyInfo{d.name, d.type, d.get(*fresh)});
      }
    }
    result.push_back(std::move(info));
  }
  return result;
}

namespace {

std::expected<const PropertyDescriptor*, std::string> find_descriptor(const ComponentRegistry& registry,
                                                                      const std::string& type_name,
                                                                      std::string_view property) {
  const auto* descriptors = registry.properties_of(type_name);
  if (descriptors == nullptr) {
    return std::unexpected("unknown component type '" + type_name + "'");
  }
  for (const PropertyDescriptor& d : *descriptors) {
    if (d.name == property) {
      return &d;
    }
  }
  return std::unexpected("unknown property '" + std::string(property) + "' on " + type_name);
}

}  // namespace

std::expected<PropertyValue, std::string> get_property_by_name(const ComponentRegistry& registry,
                                                               const std::string& type_name,
                                                               const Component& component,
                                                               std::string_view property) {
  auto found = find_descriptor(registry, type_name, property);
  if (!found) {
    return std::unexpected(found.error());
  }
  return (*found)->get(component);
}

std::expected<void, std::string> set_property_by_name(const ComponentRegistry& registry,
                                                      const std::string& type_name, Component& component,
                                                      std::string_view property, const PropertyValue& value) {
  auto found = find_descriptor(registry, type_name, property);
  if (!found) {
    return std::unexpected(found.error());
  }
  const PropertyDescriptor& d = **found;
  if (!value_fits_type(d.type, value)) {
    return std::unexpected("property '" + d.name + "' on " + type_name + " expects " +
                           std::string(to_string(d.type)));
  }
  d.set(component, value);
  return {};
}

}  // namespace arcane
