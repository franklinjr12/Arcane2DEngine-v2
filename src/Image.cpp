#pragma once
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
#include "Image.hpp"
#include <cmath>

Image::Image(std::string path, int custom_w, int custom_h, bool texture_clamp, bool flipv, bool fliph) {
	this->path = path;
	if (texture_clamp)
		texture_mode = GL_REPEAT;
	if (custom_w && custom_h)
		resize(custom_w, custom_h, flipv, fliph);
	else
		loadImage(path, flipv, fliph);
}

Image::Image(std::vector<char>& serialized_data) {
	width = *reinterpret_cast<const float*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(width));
	height = *reinterpret_cast<const float*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(height));
	texture_mode = *reinterpret_cast<const GLint*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(texture_mode));
	size_t bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
	path = std::string(serialized_data.begin(), serialized_data.begin() + bytes_size);
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
	resize(width, height);
}

Image::~Image() {
	if (texture_id != 0) {
		glDeleteTextures(1, &texture_id);
		texture_id = 0;
	}

}
void Image::loadImage(std::string path, bool flipv, bool fliph) {
	last_fliph = fliph;
	last_flipv = flipv;
	if (texture_id != 0)
		glDeleteTextures(1, &texture_id);
	int nrChannels;
	stbi_set_flip_vertically_on_load(flipv);
	int ih, iw;
	uint8_t* data = (uint8_t*)stbi_load(path.c_str(), &iw, &ih, &nrChannels, 0);
	width = iw;
	height = ih;

	glGenTextures(1, &texture_id);
	glBindTexture(GL_TEXTURE_2D, texture_id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texture_mode);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texture_mode);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if (data)
	{
		if (fliph) {
			int rowSize = width * nrChannels;
			uint8_t* tempRow = new uint8_t[rowSize];
			for (int y = 0; y < height; ++y) {
				uint8_t* row = data + y * rowSize;
				std::memcpy(tempRow, row, rowSize);
				for (int x = 0; x < width / 2; ++x) {
					for (int c = 0; c < nrChannels; ++c) {
						std::swap(row[x * nrChannels + c], row[((int)width - 1 - x) * nrChannels + c]);
					}
				}
			}
			delete[] tempRow;
		}
		if (nrChannels == 4) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		}
		else if (nrChannels == 3) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
		}
		else {
			std::cerr << "Unsupported channel count: " << nrChannels << std::endl;
		}
		glGenerateMipmap(GL_TEXTURE_2D);
		stbi_image_free(data);
	}
	else
	{
		std::cerr << "Failed to load texture" << std::endl;
		std::cerr << stbi_failure_reason() << std::endl;

	}
}

void Image::draw(Vecf pos, int w, int h) {
	draw(pos, FULL_WHITE, w, h);
}

void Image::draw(Vecf pos, RGBA_t color, float w, float h) {
	float x = pos[0];
	float y = pos[1];
	glBindTexture(GL_TEXTURE_2D, texture_id);
	glBegin(GL_QUADS);
	glColor4f(color[0], color[1], color[2], color[3]);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(x, h * 1.0f + y);
	glTexCoord2f((w / width) * 1.0f, 0.0f); glVertex2f(x + w * 1.0f, h * 1.0f + y);
	glTexCoord2f((w / width) * 1.0f, (h / height) * 1.0f); glVertex2f(x + w * 1.0f, y);
	glTexCoord2f(0.0f, (h / height) * 1.0f); glVertex2f(x, y);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f); //unset the color to not affect other stuff
	glEnd();
}

void Image::drawRotated(Vecf pos, float angle, float w, float h) {
				float x = pos[0];
				float y = pos[1];

				// Calculate the center of the rectangle
				float cx = x + w / 2.0f;
				float cy = y + h / 2.0f;

				// Precompute the sine and cosine of the angle
				float rad = angle * (3.1415 / 180.0f); // Convert angle to radians
				float cosA = cos(rad);
				float sinA = sin(rad);

				// Define the corners of the rectangle
				struct Vertex {
								float x, y;
								float tx, ty; // Texture coordinates
				};

				Vertex vertices[4] = {
								{x, y + h, 0.0f, 0.0f},
								{x + w, y + h, (w / width), 0.0f},
								{x + w, y, (w / width), (h / height)},
								{x, y, 0.0f, (h / height)}
				};

				// Apply rotation to each vertex around the center
				for (int i = 0; i < 4; ++i) {
								float newX = cosA * (vertices[i].x - cx) - sinA * (vertices[i].y - cy) + cx;
								float newY = sinA * (vertices[i].x - cx) + cosA * (vertices[i].y - cy) + cy;
								vertices[i].x = newX;
								vertices[i].y = newY;
				}

				glBindTexture(GL_TEXTURE_2D, texture_id);
				glBegin(GL_QUADS);
				glColor4f(FULL_WHITE[0], FULL_WHITE[1], FULL_WHITE[2], FULL_WHITE[3]);
				for (int i = 0; i < 4; ++i) {
								glTexCoord2f(vertices[i].tx, vertices[i].ty);
								glVertex2f(vertices[i].x, vertices[i].y);
				}
				glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // unset the color to not affect other stuff
				glEnd();
}

void Image::resize(int neww, int newh, bool flipv, bool fliph) {
	last_fliph = fliph;
	last_flipv = flipv;
	if (texture_id != 0) {
		glDeleteTextures(1, &texture_id);
		texture_id = 0;
	}
	int nrChannels;
	stbi_set_flip_vertically_on_load(flipv);
	int iw, ih;
	unsigned char* data = stbi_load(path.c_str(), &iw, &ih, &nrChannels, 0);
	width = iw;
	height = ih;
	unsigned char* resized_data = new unsigned char[neww * newh * nrChannels];

	// Perform the resize
	if (data) {
		if (fliph) {
			int rowSize = width * nrChannels;
			uint8_t* tempRow = new uint8_t[rowSize];
			for (int y = 0; y < height; ++y) {
				uint8_t* row = data + y * rowSize;
				std::memcpy(tempRow, row, rowSize);
				for (int x = 0; x < width / 2; ++x) {
					for (int c = 0; c < nrChannels; ++c) {
						std::swap(row[x * nrChannels + c], row[((int)width - 1 - x) * nrChannels + c]);
					}
				}
			}
			delete[] tempRow;
		}
		stbir_pixel_layout sp = STBIR_RGBA;
		if (nrChannels == 3) sp = STBIR_RGB;
		if (!stbir_resize_uint8_linear(data, width, height, 0, resized_data, neww, newh, 0, sp)) {
			std::cerr << "Failed to resize image" << std::endl;
		}
		stbi_image_free(data); // Free the original image data
		width = neww;
		height = newh;
	}
	else {
		std::cerr << "Failed to load texture" << std::endl;
	}


	glGenTextures(1, &texture_id);
	glBindTexture(GL_TEXTURE_2D, texture_id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texture_mode);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texture_mode);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if (resized_data)
	{
		if (nrChannels == 4) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, resized_data);
		}
		else if (nrChannels == 3) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, resized_data);
		}
		else {
			std::cerr << "Unsupported channel count: " << nrChannels << std::endl;
		}
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	else
	{
		std::cerr << "Failed to load texture" << std::endl;
	}

	stbi_image_free(resized_data);
}

void Image::flipv() {
	last_flipv = !last_flipv;
	loadImage(path, last_flipv, last_fliph);
}

void Image::fliph() {
	last_fliph = !last_fliph;
	loadImage(path, last_flipv, last_fliph);
}

std::vector<char> Image::serialize() {
	std::vector<char> v;
	const char* ptr = reinterpret_cast<const char*>(&width);
	v.insert(v.end(), ptr, ptr + sizeof(width));
	ptr = reinterpret_cast<const char*>(&height);
	v.insert(v.end(), ptr, ptr + sizeof(height));
	ptr = reinterpret_cast<const char*>(&texture_mode);
	v.insert(v.end(), ptr, ptr + sizeof(texture_mode));
	size_t bytes_size = path.size();
	ptr = reinterpret_cast<const char*>(&bytes_size);
	v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
	v.insert(v.end(), path.begin(), path.end());
	return v;
}

