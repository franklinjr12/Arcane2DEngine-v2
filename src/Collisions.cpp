#include "Collisions.hpp"
#include <cstdio>

bool isRectColliding(BodyRectangle& a, BodyRectangle& b) {
	// Check if one rectangle is to the left of the other
	if (a.pos.x + a.w < b.pos.x || b.pos.x + b.w < a.pos.x) {
		return false;
	}
	// Check if one rectangle is above the other
	if (a.pos.y + a.h < b.pos.y || b.pos.y + b.h < a.pos.y) {
		return false;
	}
	// If neither of the above are true, then the rectangles are overlapping
	return true;
}

bool isPointRectColliding(BodyRectangle& a, Point& p) {
	// Check if one rectangle is to the left of the other
	if (a.pos.x + a.w < p.x || p.x < a.pos.x) {
		return false;
	}
	// Check if one rectangle is above the other
	if (a.pos.y + a.h < p.y || p.y < a.pos.y) {
		return false;
	}
	// If neither of the above are true, then the rectangles are overlapping
	return true;
}