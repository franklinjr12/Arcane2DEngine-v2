#pragma once

#include "Arcane2DLib.hpp"
#include "Font.hpp"

#include <unordered_map>

class A2D_API FontsManager {
public:

	static FontsManager* get_instance();
	Font* get_font(std::string font_name);
	void add_font(std::string font_name, Font* font);
	Font* remove_font(std::string font_name);

	Font* default_font;

private:

	FontsManager() {
		default_font = new Font();
	}
	FontsManager(const FontsManager&) = delete;
	FontsManager& operator=(const FontsManager&) = delete;

	std::unordered_map<std::string, Font*> fonts_map;
};
