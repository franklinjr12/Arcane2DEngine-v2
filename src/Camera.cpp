#include "Camera.hpp"
#include "ArcaneTypes.hpp"

Camera::Camera(int x, int y, int w, int h) : rect(Vecf{ (float)x, (float)y }, w, h) {}

Camera::Camera(std::vector<char>& serialized_data) : rect(serialized_data) {
}

std::vector<char> Camera::serialize() {
	auto v = rect.serialize();
	return v;
}
