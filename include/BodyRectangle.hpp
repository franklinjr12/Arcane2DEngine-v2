#pragma once

#include "Arcane2DLib.hpp"
#include "ArcaneTypes.hpp"

#include <vector>

class A2D_API BodyRectangle {
public:

	BodyRectangle(Vecf pos, int width, int height);
	BodyRectangle(std::vector<char> serialized_data);
	virtual std::vector<char> serialize();
	virtual ObjectType get_type() { return ObjectType::BodyRectangle; }

	Point pos;
	int w, h;
};