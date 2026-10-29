#include "arcane/builtin_components.hpp"
#include "arcane/scene.hpp"

#include <algorithm>
#include <functional>

namespace arcane {

Node* Scene::create_node(std::string name, Node* parent) {
  return create_node_with_id(allocate_id(), std::move(name), parent);
}

Node* Scene::create_node_with_id(NodeId id, std::string name, Node* parent) {
  std::unique_ptr<Node> node(new Node(id, std::move(name)));
  Node* raw = node.get();
  node->parent_ = parent;
  if (parent) {
    parent->children_.push_back(std::move(node));
  } else {
    roots_.push_back(std::move(node));
  }
  nodes_by_id_[id] = raw;
  return raw;
}

Node* Scene::find_node(NodeId id) const noexcept {
  auto it = nodes_by_id_.find(id);
  return it == nodes_by_id_.end() ? nullptr : it->second;
}

Component* Scene::find_component(ComponentId id) const noexcept {
  auto it = components_by_id_.find(id);
  return it == components_by_id_.end() ? nullptr : it->second;
}

Component* Scene::add_component(Node* node, const std::string& type_name, const ComponentRegistry& registry) {
  if (!node || !registry.contains(type_name)) {
    return nullptr;
  }
  return add_component_with_id(node, type_name, allocate_id(), registry);
}

Component* Scene::add_component_with_id(Node* node, const std::string& type_name, ComponentId id,
                                          const ComponentRegistry& registry) {
  std::unique_ptr<Component> component = registry.create(type_name);
  if (!component) {
    return nullptr;
  }
  Component* raw = node->attach_component(std::move(component), type_name, id);
  components_by_id_[id] = raw;
  return raw;
}

bool Scene::is_descendant_of(const Node* candidate, const Node* ancestor) const noexcept {
  for (const Node* cur = candidate; cur != nullptr; cur = cur->parent()) {
    if (cur == ancestor) {
      return true;
    }
  }
  return false;
}

bool Scene::reparent(Node* node, Node* new_parent) {
  if (!node || new_parent == node) {
    return false;
  }
  // Reject if new_parent is node itself (checked above) or lives inside
  // node's own subtree -- either would create a cycle.
  if (new_parent && is_descendant_of(new_parent, node)) {
    return false;
  }

  std::vector<std::unique_ptr<Node>>& old_siblings = node->parent_ ? node->parent_->children_ : roots_;
  auto it = std::find_if(old_siblings.begin(), old_siblings.end(),
                          [node](const std::unique_ptr<Node>& candidate) { return candidate.get() == node; });
  if (it == old_siblings.end()) {
    return false;
  }
  std::unique_ptr<Node> owned = std::move(*it);
  old_siblings.erase(it);

  node->parent_ = new_parent;
  std::vector<std::unique_ptr<Node>>& new_siblings = new_parent ? new_parent->children_ : roots_;
  new_siblings.push_back(std::move(owned));
  return true;
}

namespace {

using IdMap = std::unordered_map<std::uint64_t, std::uint64_t>;

}  // namespace

Node* Scene::adopt_root(Scene& from, Node* root, const ComponentRegistry& registry) {
  if (&from == this || !root || root->parent_ || root->pending_destroy_) return nullptr;
  auto it = std::find_if(from.roots_.begin(), from.roots_.end(),
                         [root](const std::unique_ptr<Node>& candidate) { return candidate.get() == root; });
  if (it == from.roots_.end()) return nullptr;
  std::unique_ptr<Node> owned = std::move(*it);
  from.roots_.erase(it);

  IdMap ids;
  std::vector<Component*> moved;
  std::function<void(Node&)> reassign = [&](Node& node) {
    const NodeId old_node_id = node.id_;
    from.nodes_by_id_.erase(old_node_id);
    node.id_ = allocate_id();
    ids[old_node_id] = node.id_;
    nodes_by_id_[node.id_] = &node;
    for (auto& component : node.components_) {
      const ComponentId old_component_id = component->id_;
      from.components_by_id_.erase(old_component_id);
      component->id_ = allocate_id();
      ids[old_component_id] = component->id_;
      components_by_id_[component->id_] = component.get();
      moved.push_back(component.get());
    }
    for (auto& child : node.children_) reassign(*child);
  };
  reassign(*owned);
  std::function<void(Node&)> relink = [&](Node& node) {
    if (node.prefab_link_) {
      const auto mapped = ids.find(node.prefab_link_->instance_id);
      if (mapped != ids.end()) node.prefab_link_->instance_id = mapped->second;
    }
    for (auto& child : node.children_) relink(*child);
  };
  relink(*owned);
  for (Component* component : moved) {
    const auto* descriptors = registry.properties_of(component->type_name());
    if (!descriptors) continue;
    for (const PropertyDescriptor& d : *descriptors) {
      if (d.type != PropertyType::EntityId) continue;
      const auto old_id = std::get<AssetId>(d.get(*component));
      if (old_id == 0) continue;
      const auto mapped = ids.find(old_id);
      d.set(*component, PropertyValue{static_cast<AssetId>(mapped == ids.end() ? 0 : mapped->second)});
    }
  }
  Node* raw = owned.get();
  roots_.push_back(std::move(owned));
  return raw;
}

void Scene::mark_pending_destroy(Node* node) {
  if (node->pending_destroy_) {
    return;
  }
  node->pending_destroy_ = true;
  nodes_by_id_.erase(node->id_);
  for (auto& component : node->components_) {
    components_by_id_.erase(component->id());
  }
  for (auto& child : node->children_) {
    mark_pending_destroy(child.get());
  }
}

void Scene::destroy_node(NodeId id) {
  Node* node = find_node(id);
  if (!node) {
    return;
  }
  mark_pending_destroy(node);
}

void Scene::unlink_and_destroy(std::unique_ptr<Node> node) {
  // Components are destroyed children-first (post-order); the Node tree
  // itself is freed afterwards when `node` goes out of scope.
  std::function<void(Node&)> destroy_components = [&](Node& current) {
    for (auto& child : current.children_) {
      destroy_components(*child);
    }
    for (auto& component : current.components_) {
      component->on_destroy();
    }
  };
  destroy_components(*node);
}

void Scene::flush_destroyed() {
  std::function<void(std::vector<std::unique_ptr<Node>>&)> flush_container =
      [&](std::vector<std::unique_ptr<Node>>& container) {
        for (auto it = container.begin(); it != container.end();) {
          if ((*it)->pending_destroy_) {
            unlink_and_destroy(std::move(*it));
            it = container.erase(it);
          } else {
            flush_container((*it)->children_);
            ++it;
          }
        }
      };
  flush_container(roots_);
}

void Scene::collect_preorder(const std::unique_ptr<Node>& node, std::vector<Component*>& out) const {
  if (node->pending_destroy_) {
    return;
  }
  for (auto& component : node->components_) {
    out.push_back(component.get());
  }
  for (auto& child : node->children_) {
    collect_preorder(child, out);
  }
}

void Scene::collect_preorder(std::vector<Component*>& out) const {
  for (auto& root : roots_) {
    collect_preorder(root, out);
  }
}

void Scene::update(float fixed_dt) {
  std::vector<Component*> live;
  collect_preorder(live);
  for (Component* component : live) {
    if (component->node()->pending_destroy()) {
      continue;  // destroyed earlier in this same tick, by an on_update above
    }
    if (!component->started_) {
      component->started_ = true;
      component->on_start();
    }
    component->on_update(fixed_dt);
  }
  for (Component* component : live) {
    if (!component->node()->pending_destroy() && component->type_name() == "Camera2D")
      static_cast<Camera2D*>(component)->advance(*this, fixed_dt);
  }
  flush_destroyed();
}

}  // namespace arcane
