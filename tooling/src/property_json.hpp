#pragma once

#include <expected>
#include <string>

#include "arcane/property.hpp"
#include "arcane_tooling/context.hpp"

namespace arcane::tooling {

// Command-side JSON encoding of typed property values (the scene encodings,
// docs/formats/scene.md). property_from_json validates before returning and
// fails with VALUE_INVALID / NODE_NOT_FOUND (entity ids must exist).
Json property_to_json(PropertyType type, const PropertyValue& value);
std::expected<PropertyValue, ToolError> property_from_json(PropertyType type, const std::string& property_name,
                                                         const Json& json, ProjectContext& context);

}  // namespace arcane::tooling
