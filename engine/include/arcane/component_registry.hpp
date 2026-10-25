#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/property.hpp"

namespace arcane {

// Explicit name -> factory/property-list registry for Component subclasses.
// A game module registers its component types into one of these (see
// GameApi::register_types in game_api.hpp); Scene uses it to create
// components by name and the (de)serializer uses it to read/write their
// properties. Deliberately not a template-based/macro-based reflection
// system: just a map the module fills in by hand.
class ARCANE_API ComponentRegistry {
 public:
  using Factory = std::function<std::unique_ptr<Component>()>;

  void register_component(std::string type_name, Factory factory,
                            std::vector<PropertyDescriptor> properties);

  bool contains(const std::string& type_name) const noexcept;
  std::unique_ptr<Component> create(const std::string& type_name) const;
  const std::vector<PropertyDescriptor>* properties_of(const std::string& type_name) const noexcept;

 private:
  struct Entry {
    Factory factory;
    std::vector<PropertyDescriptor> properties;
  };
  std::unordered_map<std::string, Entry> entries_;
};

}  // namespace arcane
