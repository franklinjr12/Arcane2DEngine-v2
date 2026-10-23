#include "AssetsManager.hpp"

AssetsManager::AssetsManager() {
	fm = FontsManager::get_instance();
	im = ImagesManager::get_instance();
}

AssetsManager* AssetsManager::get_instance() {
	static AssetsManager am;
	return &am;
}

Font* AssetsManager::get_default_font() {
	return FontsManager::get_instance()->default_font;
}

Image* AssetsManager::get_image(std::string img_name) {
	return im->get_image(img_name);
}

void AssetsManager::add_image(std::string img_name, Image* img) {
	return im->add_image(img_name, img);
}

Image* AssetsManager::remove_image(std::string img_name) {
	return im->remove_image(img_name);
}

Font* AssetsManager::get_font(std::string font_name) {
	return fm->get_font(font_name);
}

void AssetsManager::add_font(std::string font_name, Font* font) {
	return fm->add_font(font_name, font);
}

Font* AssetsManager::remove_font(std::string font_name) {
	return fm->remove_font(font_name);
}
