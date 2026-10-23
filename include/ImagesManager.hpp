#pragma once

#include "Arcane2DLib.hpp"
#include "Image.hpp"

#include <unordered_map>

class A2D_API ImagesManager {
public:

	static ImagesManager* get_instance();
	
	Image* get_image(std::string img_name);
	void add_image(std::string img_name, Image* img);
	Image* remove_image(std::string img_name);

private:

	ImagesManager();

	std::unordered_map<std::string, Image*> images_map;

};