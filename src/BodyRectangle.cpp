#include "BodyRectangle.hpp"

BodyRectangle::BodyRectangle(Vecf pos, int width, int height) {
	this->pos.x = pos[0];
	this->pos.y = pos[1];
	w = width;
	h = height;
}

BodyRectangle::BodyRectangle(std::vector<char> serialized_data) {
	pos = *reinterpret_cast<const Point*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(Point));
	w = *reinterpret_cast<const int*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(w));
	h = *reinterpret_cast<const int*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(h));
}

std::vector<char> BodyRectangle::serialize() {
	std::vector<char> v;
	const char* ptr = reinterpret_cast<const char*>(&pos);
	v.insert(v.end(), ptr, ptr + sizeof(pos));
	ptr = reinterpret_cast<const char*>(&w);
	v.insert(v.end(), ptr, ptr + sizeof(w));
	ptr = reinterpret_cast<const char*>(&h);
	v.insert(v.end(), ptr, ptr + sizeof(h));
	return v;
}
