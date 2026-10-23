#pragma once
#include "Arcane2DLib.hpp"
#include "BodyRectangle.hpp"
#include "ArcaneTypes.hpp"

bool A2D_API isRectColliding(BodyRectangle& a, BodyRectangle& b);
bool A2D_API isPointRectColliding(BodyRectangle& a, Point& p);
