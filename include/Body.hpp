#pragma once

#include "Arcane2DLib.hpp"
#include "Image.hpp"
#include "BodyRectangle.hpp"
#include "Object.hpp"
#include "ArcaneTypes.hpp"

class A2D_API Body : public Object {
public:
	
	Body();
	Body(Image* im, BodyRectangle* rect);
	Body(std::vector<char>& serialized_data);
	~Body();
	virtual std::vector<char> serialize();
	virtual void draw() override;
	virtual void draw(int w, int h);
	void resize(int new_w, int new_h);
	virtual void update(float gravity = 0, float delta = 1);
	virtual void handle_collision(ObjectId _id) {};
	void setX(float x);
	float getX();
	void setY(float y);
	float getY();
	virtual ObjectType get_type() { return ObjectType::Body; }

	Image* image = nullptr;
	BodyRectangle* rectangle = nullptr;
	bool can_collide = true;
	bool collided = false;
	bool suffer_gravity = false;
	bool draw_rect_overlay = false;
	bool should_delete = false;

protected:

	Point pos;
};