#pragma once

#include "Arcane2DLib.hpp"
#include "ArcaneTypes.hpp"
#include "IdGenerator.hpp"
#include "EventType.hpp"
#include "ArcaneTypes.hpp"

#include <string>
#include <vector>

class A2D_API Object {
public:

	Object();
	Object(std::vector<char>& serialized_data);

	void process_serialized_data(std::vector<char>& serialized_data);
	virtual void process_events(std::vector<event_bytes_type> data) {};
	virtual void _process_events(std::vector<event_bytes_type> data) {};
	virtual void draw() {};
	virtual void _draw() {};
	virtual void update() {};
	virtual void _update() {};
	virtual std::vector<char> serialize();
	virtual ObjectType get_type() { return ObjectType::Object; }

	ObjectId id;
	std::vector<ObjectGroup> groups;
	std::string name;
};