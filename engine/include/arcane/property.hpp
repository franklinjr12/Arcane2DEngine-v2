#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "arcane/export.hpp"
#include "arcane/math2d.hpp"
#include "arcane/renderer.hpp"

namespace arcane {

class Component;

// Entity/asset references are stored as raw 64-bit IDs; PropertyType tells
// the inspector/serializer how a given std::uint64_t value should be
// interpreted (there is no separate C++ type for them, to keep the property
// value variant small).
using EntityId = std::uint64_t;
using AssetId = std::uint64_t;

enum class PropertyType { Bool, Int, Float, Vec2, Color, String, AssetId, EntityId };

ARCANE_API std::string_view to_string(PropertyType type) noexcept;

using PropertyValue = std::variant<bool, int, float, Vec2, Color, std::string, AssetId>;

// Describes one editable field of a registered component type: a stable
// name (used in the inspector and in saved scene JSON), its declared type,
// and type-erased get/set accessors. Deliberately not template-heavy: a
// small helper (make_property below) builds these from a plain data member,
// but PropertyDescriptor itself only holds std::function, so the registry
// and serializer never need to know the concrete Component subclass.
struct PropertyDescriptor {
  std::string name;
  PropertyType type;
  std::function<PropertyValue(const Component&)> get;
  std::function<void(Component&, const PropertyValue&)> set;
};

// Builds a PropertyDescriptor for a plain data member of a Component
// subclass. `type` must match the member's storage (e.g. PropertyType::Int
// for an int member, PropertyType::EntityId for a std::uint64_t member that
// holds a node ID) -- this is not checked, by design (no RTTI/metaprogramming
// beyond this one helper).
template <class T, class Member>
PropertyDescriptor make_property(std::string name, PropertyType type, Member T::*member) {
  return PropertyDescriptor{
      std::move(name), type,
      [member](const Component& component) -> PropertyValue {
        return static_cast<const T&>(component).*member;
      },
      [member](Component& component, const PropertyValue& value) {
        static_cast<T&>(component).*member = std::get<Member>(value);
      },
  };
}

}  // namespace arcane
