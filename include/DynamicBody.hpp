#pragma once

#include "Arcane2DLib.hpp"
#include "Body.hpp"
#include "ArcaneTypes.hpp"

class A2D_API DynamicBody : public Body {
public:

	
	DynamicBody();
	DynamicBody(Image* im, BodyRectangle* rect);
	DynamicBody(std::vector<char>& serialized_data);
	
	virtual void update(float gravity=0, float delta=1) override;
	virtual std::vector<char> serialize();
	virtual ObjectType get_type() { return ObjectType::DynamicBody; }

	Vecf accel;
	Vecf vel;

protected:

	std::vector<char>& from_serialized(std::vector<char>& serialized_data);
};