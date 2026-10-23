#include "Font.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

Font::Font(std::string font_path, int font_size)
{
	this->font_path = font_path;
	this->font_size = font_size;
	load_font();
}

Font::Font(std::vector<char>& serialized_data) {
	font_size = *reinterpret_cast<const int*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(font_size));
	size_t bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
	font_path = std::string(serialized_data.begin(), serialized_data.begin() + bytes_size);
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
	load_font();
}

Font::~Font() {
	delete font_cdata;
}

std::vector<char> Font::serialize() {
	std::vector<char> v;
	const char* ptr = reinterpret_cast<const char*>(&font_size);
	v.insert(v.end(), ptr, ptr + sizeof(font_size));
	size_t bytes_size = font_path.size();
	ptr = reinterpret_cast<const char*>(&bytes_size);
	v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
	v.insert(v.end(), font_path.begin(), font_path.end());
	return v;
}

void Font::load_font() {
	std::fstream infile(font_path, std::ios::binary | std::ios::in);
	infile.seekg(0, std::ios::end);
	std::streamsize file_size = infile.tellg();
	infile.seekg(0, std::ios::beg);
	font_ttf_buffer = new unsigned char[file_size];
	infile.read((char*)font_ttf_buffer, file_size);
	infile.close();
	font_temp_bitmap = new unsigned char[512 * 512];
	font_cdata = (void*) new stbtt_bakedchar[96];
	stbtt_BakeFontBitmap(font_ttf_buffer, 0, (float)font_size, font_temp_bitmap, 512, 512, 32, 96, (stbtt_bakedchar*)font_cdata);
	// can free font_ttf_buffer at this point
	delete font_ttf_buffer;
	glGenTextures(1, &font_ftex);
	glBindTexture(GL_TEXTURE_2D, font_ftex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 512, 512, 0, GL_ALPHA, GL_UNSIGNED_BYTE, font_temp_bitmap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// can free font_temp_bitmap at this point
	delete font_temp_bitmap;
}

void Font::print(Vecf pos, char* text, float r, float g, float b, float a)
{
	float x = pos[0];
	float y = pos[1];
	// assume orthographic projection with units = screen pixels, origin at top left
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, font_ftex);
	glBegin(GL_QUADS);
	glColor4f(r, g, b, a);
	while (*text) {
		if (*text >= 32 && *text < 128) {
			stbtt_aligned_quad q;
			stbtt_GetBakedQuad((stbtt_bakedchar*)font_cdata, 512, 512, *text - 32, &x, &y, &q, 1);//1=opengl & d3d10+,0=d3d9
			glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
			glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
			glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
			glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
		}
		++text;
	}
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f); //unset the color to not affect other stuff
	glEnd();
}

void Font::print(Vecf pos, char* text, RGBA_t color){
	print(pos, text, color[0], color[1], color[2], color[3]);
}
