#pragma once

#include "Arcane2DLib.hpp"
#include "DynamicBody.hpp"

class A2D_API Particle : public DynamicBody {
public:
	Particle() {}
	~Particle() {}
	bool can_collide = false;
};