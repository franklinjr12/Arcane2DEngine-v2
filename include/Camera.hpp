#pragma once

#include "Arcane2DLib.hpp"
#include "BodyRectangle.hpp"

class A2D_API Camera {
public:
	Camera(int x, int y, int w, int h);
	Camera(std::vector<char>& serialized_data);

	std::vector<char> serialize();
	virtual ObjectType get_type() { return ObjectType::Camera; }

	BodyRectangle rect;
};