#pragma once

#include "Arcane2DLib.hpp"

#include <cstdint>
#include <vector>

typedef int Veci[2];

typedef float Vecf[2];

struct Point {
	union {
		struct {
			float x, y;
		};
		Vecf pos;
	};
};

typedef uint64_t ObjectId;
typedef uint32_t ObjectGroup;

typedef float RGBA_t[4];
static RGBA_t FULL_WHITE = { 1,1,1,1 };

enum class ObjectType {
	Object,
	BodyRectangle,
	Image,
	Font,
	Camera,
	Body,
	DynamicBody,
	StaticBody,
	UiComponent,
	Button,
	ProgressBar,
	ImageDisplay,
	TextDisplay
};