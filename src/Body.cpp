
#include "Body.hpp"
#include "ArcaneUtils.hpp"

Body::Body() {
	image = nullptr;
	rectangle = nullptr;
	pos = { 0,0 };
}

Body::Body(Image* im, BodyRectangle* rect) {
	image = im;
	// center the image so that body x and y are on center
	rect->pos.x -= image->width / 2;
	rect->pos.y += image->height / 2;
	pos = { rect->pos.x, rect->pos.y };
	rectangle = rect;
}

Body::Body(std::vector<char>& serialized_data) : Object(serialized_data) {
	size_t bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
	std::vector<char> ser_img(serialized_data.begin(), serialized_data.begin() + bytes_size);
	image = new Image(ser_img);
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
	bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
	std::vector<char> ser_rect(serialized_data.begin(), serialized_data.begin() + bytes_size);
	rectangle = new BodyRectangle(ser_rect);
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
	pos = *reinterpret_cast<const Point*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(Point));
	can_collide = *reinterpret_cast<const bool*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bool));
	suffer_gravity = *reinterpret_cast<const bool*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bool));
	draw_rect_overlay = *reinterpret_cast<const bool*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bool));
}


Body::~Body() {
	if (image) delete image;
	if (rectangle) delete rectangle;
}

std::vector<char> Body::serialize() {
	std::vector<char> v = Object::serialize();
	auto ser_img = image->serialize();
	size_t ser_img_size = ser_img.size();
	const char* ptr = reinterpret_cast<const char*>(&ser_img_size);
	v.insert(v.end(), ptr, ptr + sizeof(ser_img_size));
	v.insert(v.end(), ser_img.begin(), ser_img.end());
	auto ser_rect = rectangle->serialize();
	size_t ser_rect_size = ser_rect.size();
	ptr = reinterpret_cast<const char*>(&ser_rect_size);
	v.insert(v.end(), ptr, ptr + sizeof(ser_rect_size));
	v.insert(v.end(), ser_rect.begin(), ser_rect.end());
	ptr = reinterpret_cast<const char*>(&pos);
	v.insert(v.end(), ptr, ptr + sizeof(pos));
	ptr = reinterpret_cast<const char*>(&can_collide);
	v.insert(v.end(), ptr, ptr + sizeof(can_collide));
	ptr = reinterpret_cast<const char*>(&suffer_gravity);
	v.insert(v.end(), ptr, ptr + sizeof(suffer_gravity));
	ptr = reinterpret_cast<const char*>(&draw_rect_overlay);
	v.insert(v.end(), ptr, ptr + sizeof(draw_rect_overlay));
	return v;
}

void Body::draw() {
	Vecf draw_pos = { pos.x - image->width / 2,  pos.y - image->height / 2 };
	image->draw(draw_pos, image->width, image->height);
	if (draw_rect_overlay) {
		Image& blue_overlay = GetBlueOverlayImage();
		RGBA_t color = { 1,1,1,OVERLAY_ALPHA };
		blue_overlay.draw(rectangle->pos.pos, color, rectangle->w, rectangle->h);
	}
	_draw();
}

void Body::draw(int w, int h) {
	Vecf draw_pos = { pos.x - w / 2,  pos.y - h / 2 };
	image->draw(draw_pos, w, h);
	if (draw_rect_overlay) {
		Image& blue_overlay = GetBlueOverlayImage();
		RGBA_t color = { 1,1,1,OVERLAY_ALPHA };
		blue_overlay.draw(rectangle->pos.pos, color, rectangle->w, rectangle->h);
	}
	_draw();
}

void Body::resize(int new_w, int new_h) {
	image->resize(new_w, new_h);
	rectangle->w = new_w;
	rectangle->h = new_h;
}

void Body::setX(float x) {
	pos.x = x;
	rectangle->pos.x = x - image->width / 2;
}

float Body::getX() { return pos.x; }

void Body::setY(float y) {
	pos.y = y;
	rectangle->pos.y = y - image->height / 2;
}

float Body::getY() { return pos.y; }

void Body::update(float gravity, float delta) {}

