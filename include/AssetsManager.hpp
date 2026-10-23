#pragma once

#include "Arcane2DLib.hpp"
#include "FontsManager.hpp"
#include "Image.hpp"
#include "FontsManager.hpp"
#include "ImagesManager.hpp"

// In the future this class will handle assets compression and decompression
class A2D_API AssetsManager {
public:

	static AssetsManager* get_instance();

	Font* get_default_font();
	Image* get_image(std::string img_name);
	void add_image(std::string img_name, Image* img);
	Image* remove_image(std::string img_name);
	Font* get_font(std::string font_name);
	void add_font(std::string font_name, Font* font);
	Font* remove_font(std::string font_name);


private:
	//static AssetsManager am;
	AssetsManager();
	FontsManager* fm;
	ImagesManager* im;
};