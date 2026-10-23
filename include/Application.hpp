#pragma once

#include "Arcane2DLib.hpp"
#include "Image.hpp"
#include "BodyRectangle.hpp"
#include "Body.hpp"
#include "FrameController.hpp"
#include "StaticBody.hpp"
#include "Camera.hpp"
#include "Collisions.hpp"
#include "Scene.hpp"
#include "Player.hpp"
#include "Particle.hpp"
#include "EventsManager.hpp"
#include "Button.hpp"
#include "Font.hpp"
#include "FrameController.hpp"
#include "Logger.hpp"
#include "ConfigurationManager.hpp"
#include "ArcaneUtils.hpp"
#include "Timer.hpp"

#ifdef COMPILE_IMGUI
#define IMGUI_DISABLE_STB_TRUETYPE_IMPLEMENTATION
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#endif

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>

class A2D_API Application : public Object {
public:
	Application(int width = DEFAULT_SCREEN_WIDTH, int height = DEFAULT_SCREEN_HEIGHT);
	//~Application();
	// override this method and put all your game inside
	virtual void game_loop();
	// if needed custom drawing put in here
	virtual void game_draw();

	// capture keyboard events
	void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
	// Static trampoline function to the real member function callback
	static void KeyCallbackTrampoline(GLFWwindow* window, int key, int scancode, int action, int mods);
	void mouse_callback(GLFWwindow* window, int button, int action, int mods);
	static void MouseCallbackTrampoline(GLFWwindow* window, int button, int action, int mods);
	void resolution_callback(GLFWwindow* window, int width, int height);
	static void ResolutionCallbackTrampoline(GLFWwindow* window, int width, int height);
	void setWindowCallbacks(GLFWwindow* window);
	int init();
	void run();
	void poll_events();
	void update();
	void draw();
	void process_thread();
	void change_scene(ObjectId scene_id);
	void change_scene(std::string scene_name);
	Scene* current_scene = nullptr;
	std::vector<Scene*> scenes;
	GLFWwindow* window = nullptr;
	int width, height;
#ifdef COMPILE_IMGUI
	void handle_imgui();
	GLFWwindow* window_imgui = nullptr;
#endif
	Veci mouse_pos;
	EventsManager* events_manager;
	Player* player;
	FramesController fc;
	std::chrono::system_clock::time_point current_time;
	std::chrono::system_clock::time_point last_time;
	float delta = 0;
	bool running = false;
	long process_execution_between_frames = 0;
	//std::string window_name;
};

static void glfw_error_callback(int error, const char* description) {
	A2D_LOGE("GLFW Error {} description {}", error, description);
}
