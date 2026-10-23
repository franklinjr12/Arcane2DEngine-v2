#include "DynamicBody.hpp"

DynamicBody::DynamicBody() {
	accel[0] = 0;
	accel[1] = 0;
	vel[0] = 0;
	vel[1] = 0;
}

DynamicBody::DynamicBody(Image* im, BodyRectangle* rect) : Body(im, rect) {
	accel[0] = 0;
	accel[1] = 0;
	vel[0] = 0;
	vel[1] = 0;
}

DynamicBody::DynamicBody(std::vector<char>& serialized_data) : Body(serialized_data) {
	accel[0] = reinterpret_cast<const float*>(serialized_data.data())[0];
	accel[1] = reinterpret_cast<const float*>(serialized_data.data())[1];
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(Vecf));
	vel[0] = reinterpret_cast<const float*>(serialized_data.data())[0];
	vel[1] = reinterpret_cast<const float*>(serialized_data.data())[1];
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(Vecf));
}

void DynamicBody::update(float gravity, float delta) {
	vel[0] += accel[0] * delta;
	pos.x += vel[0] * delta;
	if (suffer_gravity)
				vel[1] += (accel[1] + gravity) * delta;
	else 
				vel[1] += accel[1] * delta;
	pos.y += vel[1] * delta;
	rectangle->pos.x = pos.x;
	rectangle->pos.y = pos.y;
	_update();
}

std::vector<char> DynamicBody::serialize() {
	std::vector<char> v = Body::serialize();
	const char* ptr = reinterpret_cast<const char*>(&accel);
	v.insert(v.end(), ptr, ptr + sizeof(accel));
	ptr = reinterpret_cast<const char*>(&vel);
	v.insert(v.end(), ptr, ptr + sizeof(vel));
	return v;
}

