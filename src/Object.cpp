#include "Object.hpp"

Object::Object() {
	id = generate_id();
}

Object::Object(std::vector<char>& serialized_data) {
	process_serialized_data(serialized_data);
}

void Object::process_serialized_data(std::vector<char>& serialized_data) {
	// id
	id = *reinterpret_cast<const ObjectId*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(ObjectId));
	// groups
	size_t bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
	for (int i = 0; i < bytes_size; i++) {
		ObjectGroup og = *reinterpret_cast<const ObjectGroup*>(&serialized_data[i * sizeof(ObjectGroup)]);
		groups.push_back(og);
	}
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size * sizeof(ObjectGroup));
	// name
	bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
	name = std::string(serialized_data.begin(), serialized_data.begin() + bytes_size);
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
}

std::vector<char> Object::serialize() {
	std::vector<char> v;
	const char* ptr = reinterpret_cast<const char*>(&id);
	v.insert(v.end(), ptr, ptr + sizeof(ObjectId));
	//add number of groups
	size_t bytes_size = groups.size();
	ptr = reinterpret_cast<const char*>(&bytes_size);
	v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
	for (auto& e : groups) {
		const char* group_ptr = reinterpret_cast<const char*>(&e);
		v.insert(v.end(), group_ptr, group_ptr + sizeof(ObjectGroup));
	}
	bytes_size = name.size();
	ptr = reinterpret_cast<const char*>(&bytes_size);
	v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
	v.insert(v.end(), name.begin(), name.end());
	return v;
}