#include "Animation.hpp"

Animation::Animation() {
	last_time = std::chrono::system_clock::now();
}

Animation::Animation(std::vector<char>& serialized_data) {
	size_t data_size = *reinterpret_cast<const size_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(data_size));
	for (int i = 0; i < data_size; i++) {
		size_t img_size = *reinterpret_cast<const size_t*>(serialized_data.data());
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(data_size));
		std::vector<char> ser_img(serialized_data.begin(), serialized_data.begin() + img_size);
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + ser_img.size());
		Image* image = new Image(ser_img);
		images.push_back(image);
	}
	fps = *reinterpret_cast<const int*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(fps));
}

std::vector<char> Animation::serialize() {
	std::vector<char> v;
	size_t data_size = images.size();
	const char* ptr = reinterpret_cast<const char*>(&data_size);
	v.insert(v.end(), ptr, ptr + sizeof(data_size));
	for (auto* image : images) {
		auto ser_img = image->serialize();
		size_t ser_img_size = ser_img.size();
		ptr = reinterpret_cast<const char*>(&ser_img_size);
		v.insert(v.end(), ptr, ptr + sizeof(ser_img_size));
		v.insert(v.end(), ser_img.begin(), ser_img.end());
	}
	ptr = reinterpret_cast<const char*>(&fps);
	v.insert(v.end(), ptr, ptr + sizeof(fps));
	return v;
}


bool Animation::has_animation() {
	return images.size();
}

void Animation::next_frame() {
	auto diff = std::chrono::system_clock::now() - last_time;
	auto count = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
	if (count == 0) return;
	if ((1000 / count) > fps)
		return;
	last_time = std::chrono::system_clock::now();
	current_frame++;
	if (current_frame >= images.size())
		current_frame = 0;
}

void Animation::add_animation(Image& img) {
	images.push_back(&img);
}