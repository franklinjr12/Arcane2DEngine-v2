#include "ImageDisplay.hpp"

ImageDisplay::ImageDisplay(Vecf pos, Image* image) {
	this->pos[0] = pos[0];
	this->pos[1] = pos[1];
	this->image = image;
	w = image->width;
	h = image->height;
	rect = new BodyRectangle(pos, w, h);
}

ImageDisplay::ImageDisplay(std::vector<char>& serialized_data) : UiComponent(serialized_data) {

}

std::vector<char> ImageDisplay::serialize() {
	auto v = UiComponent::serialize();
	return v;
}

void ImageDisplay::draw() {
	image->draw(pos, w, h);
}
