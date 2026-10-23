#include "FrameController.hpp"
#include <thread>


FramesController::FramesController(int fps) : fps(fps) {
}
void FramesController::frameBegin() {
	frame_begin = std::chrono::system_clock::now();
}
void FramesController::frameEnd() {
	frame_end = std::chrono::system_clock::now();
}
void FramesController::sleep() {
	auto diff = frame_end - frame_begin;
	frame_ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
	if (frame_ms > 0) {
		real_fps = 1000 / frame_ms;
		if (fps - frame_ms > 0) {
			sleep_time = 1000 / (fps - frame_ms);
			if (sleep_time > 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
			}
		}
	}
}
