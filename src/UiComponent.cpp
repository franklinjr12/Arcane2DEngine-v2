#include "UiComponent.hpp"

A2D_API Object* GetObjectFromCollectionByName(std::forward_list<UiComponent*>& collection, const std::string& name) {
	for (auto* e : collection)
		if (((Object*)e)->name == name) return e;
	return nullptr;
}