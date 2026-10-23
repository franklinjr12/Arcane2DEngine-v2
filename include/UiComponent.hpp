#pragma once

#include "Arcane2DLib.hpp"
#include "Object.hpp"
#include "BodyRectangle.hpp"
#include "Animation.hpp"

#include <forward_list>

class A2D_API UiComponent : public Object {
public:

	UiComponent() {}

	UiComponent(std::vector<char>& serialized_data) : Object(serialized_data) {
		size_t bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
		std::vector<char> ser_img(serialized_data.begin(), serialized_data.begin() + bytes_size);
		image = new Image(ser_img);
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
		bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
		std::vector<char> ser_rect(serialized_data.begin(), serialized_data.begin() + bytes_size);
		rect = new BodyRectangle(ser_rect);
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
		has_animation = *reinterpret_cast<const bool*>(serialized_data.data());
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(has_animation));
		if (has_animation) {
			bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
			serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
			std::vector<char> ser_animation(serialized_data.begin(), serialized_data.begin() + bytes_size);
			serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
			animation = new Animation(ser_animation);
		}
		pos[0] = reinterpret_cast<const float*>(serialized_data.data())[0];
		pos[1] = reinterpret_cast<const float*>(serialized_data.data())[1];
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(pos));
		should_draw = *reinterpret_cast<const bool*>(serialized_data.data());
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(should_draw));
		w = *reinterpret_cast<const float*>(serialized_data.data());
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(w));
		h = *reinterpret_cast<const float*>(serialized_data.data());
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(h));
	}

	virtual std::vector<char> serialize() {
		std::vector<char> v = Object::serialize();
		auto ser_img = image->serialize();
		size_t ser_img_size = ser_img.size();
		const char* ptr = reinterpret_cast<const char*>(&ser_img_size);
		v.insert(v.end(), ptr, ptr + sizeof(ser_img_size));
		v.insert(v.end(), ser_img.begin(), ser_img.end());
		auto ser_rect = rect->serialize();
		size_t ser_rect_size = ser_rect.size();
		ptr = reinterpret_cast<const char*>(&ser_rect_size);
		v.insert(v.end(), ptr, ptr + sizeof(ser_rect_size));
		v.insert(v.end(), ser_rect.begin(), ser_rect.end());
		ptr = reinterpret_cast<const char*>(&has_animation);
		v.insert(v.end(), ptr, ptr + sizeof(has_animation));
		if (has_animation) {
			auto ser_animation = animation->serialize();
			size_t ser_animation_size = ser_animation.size();
			ptr = reinterpret_cast<const char*>(&ser_animation_size);
			v.insert(v.end(), ptr, ptr + sizeof(ser_animation_size));
			v.insert(v.end(), ser_animation.begin(), ser_animation.end());
		}
		ptr = reinterpret_cast<const char*>(&pos);
		v.insert(v.end(), ptr, ptr + sizeof(pos));
		ptr = reinterpret_cast<const char*>(&should_draw);
		v.insert(v.end(), ptr, ptr + sizeof(should_draw));
		ptr = reinterpret_cast<const char*>(&w);
		v.insert(v.end(), ptr, ptr + sizeof(w));
		ptr = reinterpret_cast<const char*>(&h);
		v.insert(v.end(), ptr, ptr + sizeof(h));
		return v;
	}

	virtual void on_click() {};
	virtual void mouse_over() {};
	void resize(int new_w, int new_h) {
		rect->w = new_w;
		rect->h = new_h;
		image->resize(new_w, new_h);
	}
	virtual void update() override {
		_update();
	}
	virtual ObjectType get_type() { return ObjectType::UiComponent; }

	bool has_animation = false;
	bool should_draw = true;
	Vecf pos;
	float w, h;
	Image* image;
	BodyRectangle* rect;
	Animation* animation;
};

A2D_API Object* GetObjectFromCollectionByName(std::forward_list<UiComponent*>& collection, const std::string& name);
