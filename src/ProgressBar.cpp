#include "ProgressBar.hpp"

ProgressBar::ProgressBar(Vecf pos, Image* back, Image* front, int min, int max) {
	this->pos[0] = pos[0];
	this->pos[1] = pos[1];
	image = back;
	progress_back = back;
	progress_front = front;
	original_width = front->width;
	this->min = min;
	this->max = max;
	current = max;
	last_current = current;
	rect = new BodyRectangle(pos, back->width, back->height);
}

ProgressBar::ProgressBar(std::vector<char> serialized_data) : UiComponent(serialized_data) {
	size_t bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
	auto v = std::vector<char>(serialized_data.begin(), serialized_data.begin() + bytes_size);
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
	progress_back = new Image(v);
	bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
	v = std::vector<char>(serialized_data.begin(), serialized_data.begin() + bytes_size);
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
	progress_front = new Image(v);
	min = *reinterpret_cast<const int*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(min));
	max = *reinterpret_cast<const int*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(max));
	current = *reinterpret_cast<const int*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(current));
	last_current = *reinterpret_cast<const int*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(last_current));
	original_width = *reinterpret_cast<const int*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(original_width));
}

std::vector<char> ProgressBar::serialize() {
	auto v = UiComponent::serialize();
	auto ser_img = progress_back->serialize();
	size_t bytes_size = ser_img.size();
	const char* ptr = reinterpret_cast<const char*>(&bytes_size);
	v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
	v.insert(v.end(), ser_img.begin(), ser_img.end());
	ser_img = progress_front->serialize();
	bytes_size = ser_img.size();
	ptr = reinterpret_cast<const char*>(&bytes_size);
	v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
	v.insert(v.end(), ser_img.begin(), ser_img.end());
	ptr = reinterpret_cast<const char*>(&min);
	v.insert(v.end(), ptr, ptr + sizeof(min));
	ptr = reinterpret_cast<const char*>(&max);
	v.insert(v.end(), ptr, ptr + sizeof(max));
	ptr = reinterpret_cast<const char*>(&current);
	v.insert(v.end(), ptr, ptr + sizeof(current));
	ptr = reinterpret_cast<const char*>(&last_current);
	v.insert(v.end(), ptr, ptr + sizeof(last_current));
	ptr = reinterpret_cast<const char*>(&original_width);
	v.insert(v.end(), ptr, ptr + sizeof(original_width));
	return v;
}

void ProgressBar::draw() {
	if (should_draw) {
		progress_back->draw(pos, progress_back->width, progress_front->height);
		if (current < 1) current = 1;
		if (last_current != current) {
			last_current = current;
			float temp = current;
			temp = (temp * original_width) / max;
			progress_front->resize(temp, progress_front->height);
		}
		float temp = current;
		temp = (temp * original_width) / max;
		progress_front->draw(pos, temp, progress_front->height);
	}
}

void ProgressBar::set_max(int nmax) {
	max = nmax;
}

void ProgressBar::set_current(int ncurrent) {
	current = ncurrent;
}
