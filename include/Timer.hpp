#pragma once

#include "Object.hpp"

#include <thread>
#include <chrono>

class A2D_API Timer : public Object {
public:
	
	Timer(uint64_t timeout_ms, bool repeat = false);

	void start();
	void stop();
	void reset();

	std::chrono::milliseconds timeout;
	std::chrono::milliseconds current;
	bool should_repeat;
	bool should_stop = false;
	std::thread* t = nullptr;
};

void timer_thread(Timer* timer);
