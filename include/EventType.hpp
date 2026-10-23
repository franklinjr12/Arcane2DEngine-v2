#pragma once

#include "Arcane2DLib.hpp"

#include <iostream>
#include <vector>

enum class A2D_API EventType {
	KeyboardInput,
	MouseInput,
	ButtonClicked,
	Timer,
	SceneChanged
};

typedef uint64_t event_bytes_type;

struct A2D_API EventData {
	EventType type;
	std::vector<event_bytes_type> data;
};

struct A2D_API CustomEventData {
	std::string name;
	std::vector<event_bytes_type> data;
};
