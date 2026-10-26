#include "arcane/component_registry.hpp"

namespace arcane {

void ComponentRegistry::register_component(std::string type_name, Factory factory,
                                             std::vector<PropertyDescriptor> properties) {
  entries_[std::move(type_name)] = Entry{std::move(factory), std::move(properties)};
}

bool ComponentRegistry::contains(const std::string& type_name) const noexcept {
  return entries_.find(type_name) != entries_.end();
}

std::unique_ptr<Component> ComponentRegistry::create(const std::string& type_name) const {
  auto it = entries_.find(type_name);
  if (it == entries_.end()) {
    return nullptr;
  }
  return it->second.factory();
}

const std::vector<PropertyDescriptor>* ComponentRegistry::properties_of(const std::string& type_name) const noexcept {
  auto it = entries_.find(type_name);
  if (it == entries_.end()) {
    return nullptr;
  }
  return &it->second.properties;
}

std::vector<std::string> ComponentRegistry::type_names() const {
  std::vector<std::string> names;
  names.reserve(entries_.size());
  for (const auto& [name, entry] : entries_) {
    names.push_back(name);
  }
  return names;
}

}  // namespace arcane
