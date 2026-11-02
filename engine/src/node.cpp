#include "arcane/node.hpp"

#include <cmath>

namespace arcane {

namespace {
WorldTransform2D compose(const WorldTransform2D& parent, const Transform2D& local) {
  const float cos_r = std::cos(parent.rotation_radians);
  const float sin_r = std::sin(parent.rotation_radians);
  const float scaled_x = local.position.x * parent.scale.x;
  const float scaled_y = local.position.y * parent.scale.y;

  WorldTransform2D out;
  out.position.x = parent.position.x + (scaled_x * cos_r - scaled_y * sin_r);
  out.position.y = parent.position.y + (scaled_x * sin_r + scaled_y * cos_r);
  out.rotation_radians = parent.rotation_radians + local.rotation_radians;
  out.scale.x = parent.scale.x * local.scale.x;
  out.scale.y = parent.scale.y * local.scale.y;
  return out;
}
}  // namespace

WorldTransform2D Node::world_transform() const noexcept {
  if (!parent_) {
    return local_;
  }
  return compose(parent_->world_transform(), local_);
}

bool Node::active_in_hierarchy() const noexcept {
  for (const Node* node = this; node; node = node->parent_) {
    if (!node->active_) return false;
  }
  return true;
}

Component* Node::find_component(std::string_view type_name) const noexcept {
  for (const auto& component : components_) {
    if (component->type_name() == type_name) {
      return component.get();
    }
  }
  return nullptr;
}

Component* Node::attach_component(std::unique_ptr<Component> component, std::string type_name,
                                   ComponentId component_id) {
  component->id_ = component_id;
  component->node_ = this;
  component->type_name_ = std::move(type_name);
  Component* raw = component.get();
  components_.push_back(std::move(component));
  raw->on_create();
  return raw;
}

}  // namespace arcane
