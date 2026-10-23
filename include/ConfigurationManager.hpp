#pragma once

#include <iostream>

class ConfigurationManager {
public:

	static ConfigurationManager& get_instance();
	void save();
	int get_resolution_width();
	int get_resolution_height();
	int get_fps();

private:

	static ConfigurationManager self;
	ConfigurationManager();

	const std::string ini_path = "config.ini";
	int resolution_width = -1;
	int resolution_height = -1;
	int fps = -1;
};