#pragma once

#include "Arcane2DLib.hpp"
#include "ArcaneTypes.hpp"

#include <GL/glew.h>
#include <fstream>
#include <vector>

class A2D_API Font {
public:
	Font(std::string font_path = "assets/16020_FUTURAM.ttf", int font_size = 32);
	Font(std::vector<char>& serialized_data);
	~Font();

	virtual std::vector<char> serialize();
	void print(Vecf pos, char* text, float r = 0, float g = 0, float b = 0, float a = 1);
	void print(Vecf pos, char* text, RGBA_t color);

	unsigned char* font_ttf_buffer;
	unsigned char* font_temp_bitmap;
	//stbtt_bakedchar font_cdata[96]; // ASCII 32..126 is 95 glyphs
	void* font_cdata;
	GLuint font_ftex;
	std::string font_path;
	int font_size;

protected:
	void load_font();
};
