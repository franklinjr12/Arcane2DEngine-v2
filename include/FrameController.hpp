#pragma once

#include "Arcane2DLib.hpp"
#include <chrono>

class A2D_API FramesController {
public:
	FramesController(int fps);
	void frameBegin();
	void frameEnd();
	void sleep();
	int fps = 0;
	long long frame_ms = 0;
	long long sleep_time = 0;
	long long real_fps = 0;
	std::chrono::system_clock::time_point frame_begin;
	std::chrono::system_clock::time_point frame_end;
};