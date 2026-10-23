#include "ConfigurationManager.hpp"

#include <inicpp.h>


ConfigurationManager ConfigurationManager::self;

ConfigurationManager& ConfigurationManager::get_instance() {
	return self;
}

ConfigurationManager::ConfigurationManager() {
	ini::IniFile config;
	config.load(ini_path);
	resolution_width = config["display"]["width"].as<int>();
	resolution_height = config["display"]["height"].as<int>();
	fps = config["display"]["fps"].as<int>();
}

void ConfigurationManager::save() {
	ini::IniFile config;
	config["display"]["width"] = resolution_width;
	config["display"]["height"] = resolution_height;
	config["display"]["fps"] = fps;
	config.save(ini_path);
}

int ConfigurationManager::get_resolution_width() {
	return resolution_width;
}

int ConfigurationManager::get_resolution_height() {
	return resolution_height;
}

int ConfigurationManager::get_fps() {
	return fps;
}
