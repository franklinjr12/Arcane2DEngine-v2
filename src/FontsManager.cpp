#include "FontsManager.hpp"

A2D_API FontsManager* FontsManager::get_instance() {
	static FontsManager fm;
	return &fm;
}

Font* FontsManager::get_font(std::string font_name) {
	try {
		if (fonts_map.at(font_name))
			return fonts_map.at(font_name);
	}
	catch (std::out_of_range) {
		Font* font = new Font("assets/" + font_name);
		if (font) {
			fonts_map[font_name] = font;
			return font;
		}
	}
	return nullptr;
}

void FontsManager::add_font(std::string font_name, Font* font) {
	fonts_map[font_name] = font;
}

Font* FontsManager::remove_font(std::string font_name) {
	Font* f = fonts_map.at(font_name);
	if (f)
		fonts_map.erase(font_name);
	return f;
}