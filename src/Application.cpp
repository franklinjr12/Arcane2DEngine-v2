#include "Application.hpp"

#include <thread>
#include <stdio.h>
#include <assert.h>

Application::Application(int width, int height) : fc(DEFAULT_FPS) {
	this->width = width;
	this->height = height;
	//window_name = name;
	events_manager = EventsManager::getInstance();
	auto conf = ConfigurationManager::get_instance();
	if (conf.get_resolution_height() > 0)
		this->height = conf.get_resolution_height();
	if (conf.get_resolution_width() > 0)
		this->width = conf.get_resolution_width();
	if (conf.get_fps() > 0)
		fc.fps = conf.get_fps();
	A2D_LOGI("Using resolution: {} x {} fps: {}", this->width, this->height, fc.fps);
	init();
}

//Application::~Application(){}

int Application::init() {
	set_arcane_logger();

	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		return -1;

	
#ifdef COMPILE_IMGUI
	window_imgui = glfwCreateWindow(width, height, "Arcane2D Debug", NULL, NULL);
#endif
	window = glfwCreateWindow(width, height, "Arcane2D", NULL, NULL);
	if (!window)
	{
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(window);
	if (glewInit() != GLEW_OK) {
		std::cout << "Failed to initialize GLEW" << std::endl;
		return -1;
	}

	// Set up the orthogonal projection
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, width, height, 0.0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// Enable 2D texturing
	glEnable(GL_TEXTURE_2D);
	// Enable blending
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	setWindowCallbacks(window);

#ifdef COMPILE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window_imgui, true);          // Second param install_callback=true will install GLFW callbacks and chain to existing ones.
	ImGui_ImplOpenGL3_Init();
#endif

	return 0;
}

void Application::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	EventData ed;
	ed.type = EventType::KeyboardInput;
	ed.data.push_back((event_bytes_type)EventType::KeyboardInput);
	ed.data.push_back((event_bytes_type)action);
	ed.data.push_back((event_bytes_type)key);
	events_manager->events_data.push(ed);
}

// Static trampoline function to the real member function callback
void Application::KeyCallbackTrampoline(GLFWwindow* window, int key, int scancode, int action, int mods) {
	// Retrieve the instance of Application class associated with this window
	void* ptr = glfwGetWindowUserPointer(window);
	Application* app = static_cast<Application*>(ptr);

	// Call the member function key_callback
	app->key_callback(window, key, scancode, action, mods);
}

void Application::mouse_callback(GLFWwindow* window, int button, int action, int mods) {
	printf("Application::mouse_callback");
	EventData ed;
	ed.type = EventType::MouseInput;
	ed.data.push_back((event_bytes_type)ed.type);
	ed.data.push_back((event_bytes_type)action);
	ed.data.push_back((event_bytes_type)button);
	ed.data.push_back((event_bytes_type)mouse_pos[0]);
	ed.data.push_back((event_bytes_type)mouse_pos[1]);
	events_manager->events_data.push(ed);
}

// Static trampoline function to the real member function callback
void Application::MouseCallbackTrampoline(GLFWwindow* window, int button, int action, int mods) {
	// Retrieve the instance of Application class associated with this window
	void* ptr = glfwGetWindowUserPointer(window);
	Application* app = static_cast<Application*>(ptr);

	// Call the member function key_callback
	app->mouse_callback(window, button, action, mods);
}

void Application::resolution_callback(GLFWwindow* window, int width, int height) {
	this->width = width;
	this->height = height;
	A2D_LOGI("Resolution changed: {} x {}", width, height);
	current_scene->resolution_changed(width, height);
}

void Application::ResolutionCallbackTrampoline(GLFWwindow* window, int width, int height) {
	void* ptr = glfwGetWindowUserPointer(window);
	Application* app = static_cast<Application*>(ptr);
	app->resolution_callback(window, width, height);
}

void Application::setWindowCallbacks(GLFWwindow* window) {
	glfwSetWindowUserPointer(window, this); // Set this instance as the user pointer
	glfwSetKeyCallback(window, Application::KeyCallbackTrampoline); // Set the trampoline as the key callback
	glfwSetMouseButtonCallback(window, Application::MouseCallbackTrampoline);
	glfwSetFramebufferSizeCallback(window, Application::ResolutionCallbackTrampoline);
}

void Application::poll_events() {
	// Poll and handle events (inputs, window resize, etc.)
	glfwPollEvents();
	events_manager->run();
	double mx, my;
	glfwGetCursorPos(window, &mx, &my);
	mouse_pos[0] = (int)mx;
	mouse_pos[1] = (int)my;
}

void Application::update() {
				current_time = std::chrono::system_clock::now();
				auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(current_time - last_time).count();
				delta = diff * 1.0e-9;
				current_scene->update(mouse_pos, delta);
				last_time = std::chrono::system_clock::now();
}

void Application::game_loop() {}
void Application::game_draw() {}

void Application::draw() {
	glfwMakeContextCurrent(window); // Make the game window's context current
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	//glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	//glClearColor(1.0f, 0.0f, 0.0f, 0.0f);
	current_scene->draw();
}

void Application::process_thread() {
				const int MAX_UPDATES = 300;
				while (running) {
								update();
								game_loop();
								std::this_thread::sleep_for(std::chrono::microseconds(1));
								//process_execution_between_frames++;
								//if (process_execution_between_frames > MAX_UPDATES) {
								//				while (process_execution_between_frames != 0) { // waits for main thread to clear
								//								std::this_thread::sleep_for(std::chrono::milliseconds(1));
								//				}
								//}
				}
}

EventData create_change_scene_event(Scene* s) {
	EventData ed;
	ed.type = EventType::SceneChanged;
	ed.data.push_back((event_bytes_type)EventType::SceneChanged);
	ed.data.push_back((event_bytes_type)s->name.size());
	for (auto c : s->name) {
		ed.data.push_back((event_bytes_type)c);
	}
	return ed;
}

void Application::change_scene(ObjectId scene_id) {
	for (Scene* s : scenes) {
		if (s->id == scene_id) {
			EventData ed = create_change_scene_event(s);
			events_manager->events_data.push(ed);
			current_scene = s;
			return;
		}
	}
}

void Application::change_scene(std::string scene_name) {
	for (Scene* s : scenes) {
		if (s->name == scene_name) {
			EventData ed = create_change_scene_event(s);
			events_manager->events_data.push(ed);
			current_scene->on_scene_exited();
			current_scene = s;
			current_scene->on_scene_entered();
			return;
		}
	}
}

#ifdef COMPILE_IMGUI
void Application::handle_imgui() {
	// Start the Dear ImGui frame
	glfwMakeContextCurrent(window_imgui); // Make the ImGui window's context current
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	// Here you can do your ImGui rendering
	ImGui::ShowDemoWindow(); // Show demo window!
	// Render ImGui
	ImGui::Render();
	glClear(GL_COLOR_BUFFER_BIT); // Clear the framebuffer
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	// Swap the buffers for the ImGui window
	glfwSwapBuffers(window_imgui);
}
#endif

//#define MULTI

void Application::run() {
	assert(current_scene != nullptr);
	assert(events_manager != nullptr);
	running = true;
	// for multithread uncomment the next 2 lines
#ifdef MULTI
	std::thread update_thread(&Application::process_thread, this);
	update_thread.detach();
#endif
	while (!glfwWindowShouldClose(window)) {
		fc.frameBegin();
		poll_events();
		// for multithread comment the next 2 lines
#ifndef MULTI
		update();
		game_loop();
#endif
		draw();
		game_draw();
		// Swap the buffers for the game window
		glfwSwapBuffers(window);
		// handle the logging of those errors appropriatedly
		GLenum err;
		while ((err = glGetError()) != GL_NO_ERROR) {
			A2D_LOGE("OpenGL error: {}", err);
		}
#ifdef COMPILE_IMGUI
		handle_imgui();
#endif
		fc.frameEnd();
		fc.sleep();
		//printf("Number of updates %lu\n", process_execution_between_frames);
		process_execution_between_frames = 0;
	}
	running = false;
#ifdef COMPILE_IMGUI
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window_imgui); // Destroy the ImGui window
#endif

	// Cleanup
	glfwDestroyWindow(window); // Destroy the game window
	glfwTerminate();
}
