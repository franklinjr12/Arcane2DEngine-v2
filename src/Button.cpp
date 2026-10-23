#include "Button.hpp"

#include "EventsManager.hpp"
#include "Logger.hpp"

Button::Button(Vecf pos, Image* image, float width, float height, std::string text, Font* font) {
	this->pos[0] = pos[0];
	this->pos[1] = pos[1];
	font_pos[0] = pos[0];
	font_pos[1] = pos[1];
	this->image = image;
	this->text = text;
	this->font = font;
	if (font != nullptr)
		has_font = true;
	if (width > 0 && height > 0) {
		w = width;
		h = height;
		rect = new BodyRectangle(pos, w, h);
	}
	else {
		w = image->width;
		h = image->height;
		rect = new BodyRectangle(pos, w, h);
	}
}

Button::Button(std::vector<char>& serialized_data) : UiComponent(serialized_data) {
	has_font = *reinterpret_cast<const bool*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(has_font));
	if (has_font) {
		size_t bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
		font = new Font(serialized_data);
		font_pos[0] = reinterpret_cast<const float*>(serialized_data.data())[0];
		font_pos[1] = reinterpret_cast<const float*>(serialized_data.data())[1];
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(font_pos));
		bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
		text = std::string(serialized_data.begin(), serialized_data.begin() + bytes_size);
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
	}
}

std::vector<char> Button::serialize() {
	auto v = UiComponent::serialize();
	const char* ptr = reinterpret_cast<const char*>(&has_font);
	v.insert(v.end(), ptr, ptr + sizeof(has_font));
	if (has_font) {
		auto ser_font = font->serialize();
		size_t bytes_size = ser_font.size();
		ptr = reinterpret_cast<const char*>(&bytes_size);
		v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
		v.insert(v.end(), ser_font.begin(), ser_font.end());
		ptr = reinterpret_cast<const char*>(font_pos);
		v.insert(v.end(), ptr, ptr + sizeof(font_pos));
		bytes_size = text.size();
		ptr = reinterpret_cast<const char*>(&bytes_size);
		v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
		v.insert(v.end(), text.begin(), text.end());
	}
	return v;
}


void Button::draw() {
	if (should_draw) {
		image->draw(pos, w, h);
		if (font != nullptr && text != "")
			font->print(font_pos, (char*)text.c_str());
	}
}

void Button::on_click() {
	A2D_LOGI("Button {} clicked {}", id, name);
	EventData ed;
	ed.type = EventType::ButtonClicked;
	ed.data.push_back((event_bytes_type)ed.type);
	ed.data.push_back((event_bytes_type)id);
	EventsManager::getInstance()->events_data.push(ed);
}
