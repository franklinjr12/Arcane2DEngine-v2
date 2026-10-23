#include "Timer.hpp"

#include <EventsManager.hpp>
#include <cstdio>

using namespace std::chrono;

Timer::Timer(uint64_t timeout_ms, bool repeat) {
	char buffer[21];
	sprintf_s(buffer, 21, "%lu", (uint64_t)id);
	name = "timer" + std::string(buffer);
	should_repeat = repeat;
	timeout = timeout_ms * 1ms;
}

void Timer::start() {
	should_stop = false;
	current = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
	if (!t) {
		t = new std::thread(timer_thread, this);
	}
}

void Timer::stop() {
	should_stop = true;
	if (t) {
		t->join();
		delete t;
		t = nullptr;
	}
}

void Timer::reset(){
	stop();
	start();
}

void timer_thread(Timer* timer) {
	while (!timer->should_stop) {
		auto now = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
		if (now - timer->current >= timer->timeout) {
			EventData ed;
			ed.type = EventType::Timer;
			ed.data.push_back((event_bytes_type)EventType::Timer);
			ed.data.push_back((event_bytes_type)timer->id);
			EventsManager::getInstance()->events_data.push(ed);
			if (timer->should_repeat)
				timer->current = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
			else {
				break;
			}
		}
	}
}