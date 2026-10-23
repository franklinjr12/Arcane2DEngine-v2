#include "TextDisplay.hpp"

TextDisplay::TextDisplay(Vecf pos, Image* image, std::string text, Font* font) {
	this->pos[0] = pos[0];
	this->pos[1] = pos[1];
	this->image = image;
	this->text = text;
	this->font = font;
	w = image->width;
	h = image->height;
	rect = new BodyRectangle(pos, w, h);
	font_pos[0] = pos[0];
	font_pos[1] = pos[1];
}

TextDisplay::TextDisplay(std::vector<char> serialized_data) : UiComponent(serialized_data) {
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

std::vector<char> TextDisplay::serialize() {
	auto v = UiComponent::serialize();
	auto ser_font = font->serialize();
	size_t bytes_size = ser_font.size();
	const char* ptr = reinterpret_cast<const char*>(&bytes_size);
	v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
	v.insert(v.end(), ser_font.begin(), ser_font.end());
	ptr = reinterpret_cast<const char*>(font_pos);
	v.insert(v.end(), ptr, ptr + sizeof(font_pos));
	bytes_size = text.size();
	ptr = reinterpret_cast<const char*>(&bytes_size);
	v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
	v.insert(v.end(), text.begin(), text.end());
	return v;
}

void TextDisplay::draw() {
	if (should_draw) {
		image->draw(pos, w, h);
		if (font != nullptr && text != "") {
			font->print(font_pos, (char*)text.c_str());
		}
	}
}