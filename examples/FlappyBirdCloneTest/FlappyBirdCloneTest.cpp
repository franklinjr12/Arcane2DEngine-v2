// Built with Arcane engine version 0.2.0.4

#include <ArcaneVersion.hpp>
#include <Application.hpp>
#include <FontsManager.hpp>

#include <iostream>
#include <chrono>
#include <cstdlib>

using namespace std::chrono;

const std::string bird_name = "bird";
const std::string tunnel_name = "tunnel";
const std::string game_over_name = "game over";

static Scene* game_over_scene = nullptr;

class Bird : public Player {
public:

	Bird() {
		idle = new Image("assets/bird.png");
		up = new Image("assets/bird_up.png");
		down = new Image("assets/bird_down.png");
		image = idle;
		Vecf initial_pos = { 60 + image->width, DEFAULT_SCREEN_HEIGHT / 2 };
		rectangle = new BodyRectangle(initial_pos, image->width, image->height);
		name = bird_name;
		setX(initial_pos[0]);
		setY(initial_pos[1]);
		suffer_gravity = true;

	}

	void _update() override {
		if (vel[1] > 5)
			vel[1] = 5;
		else if (vel[1] < -5)
			vel[1] = -5;
		if (vel[1] >= velocity_thresh)
			image = down;
		else if (vel[1] <= -velocity_thresh)
			image = up;
		else
			image = idle;
		rectangle->w = image->width;
		rectangle->h = image->height;
	}

	void process_events(std::vector<event_bytes_type> data) override {
		switch (data[0]) {
		case (event_bytes_type)EventType::KeyboardInput:
			if (data[1] == GLFW_PRESS) {
				switch (data[2]) {
				case GLFW_KEY_SPACE:
					vel[1] += vel_up;
					break;
				default:
					break;
				}
			}
		}

	}


	const float vel_up = -10;
	Image* down;
	Image* up;
	Image* idle;
	const float velocity_thresh = 3;
};

class Tunnel : public DynamicBody {
public:

	Tunnel(bool top = false, int height = 186) {
		const int default_width = 57;
		if (top)
			image = new Image("assets/fbird_tunnel.png", default_width, height, false, false);
		else
			image = new Image("assets/fbird_tunnel.png", default_width, height);
		Vecf initial_pos = { DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT - image->height };
		if (top)
			initial_pos[1] = 0;
		rectangle = new BodyRectangle(initial_pos, image->width, image->height);
		name = tunnel_name;
		setX(initial_pos[0]);
		setY(initial_pos[1]);
		suffer_gravity = false;
		vel[0] = xvel;
	}

	const float xvel = -5;
	bool passed_player = false;
};

class FlappyBird : public Application {
public:

	FlappyBird() {
		auto now = system_clock::now();
		tp = now;
		srand(now.time_since_epoch().count());
	}

	void game_loop() override {
		if (current_scene == game_over_scene) return;
		auto t = system_clock::now() - tp;
		auto count = duration_cast<milliseconds>(t).count();
		if (count > tunnel_spawn_time_ms) {
			body_count++;
			tp = system_clock::now();
			int tunnel_height = rand() % max_tunnel_height;
			if (tunnel_height < min_tunnel_height)
				tunnel_height = min_tunnel_height;
			Tunnel* t = new Tunnel(last_tunnel_top, tunnel_height);
			current_scene->add_body(t);
			last_tunnel_top = !last_tunnel_top;
		}
		auto current = current_scene->bodies.begin();
		while (current != current_scene->bodies.end()) {
			Body* const temp = *current;
			current++;
			// check player collision to game over
			if (temp->collided) {
				current_scene = game_over_scene;
				break;
			}
			// check scoring
			if (temp->name == tunnel_name) {
				Tunnel* t = (Tunnel*)temp;
				if (!t->passed_player && t->getX() < player->getX()) {
					t->passed_player = true;
					score++;
				}
			}
			const int offscreen = -100;
			if (temp->getX() < offscreen) {
				body_count--;
				current_scene->remove_body(temp->id);
				delete temp;
			}
		}
	}

	void game_draw() override {
		static const int buf_size = 128;
		char text_buffer[buf_size];
		Font* font = FontsManager::get_instance()->default_font;
		Vecf font_pos;
		font_pos[0] = 10;
		font_pos[1] = 90;
		sprintf_s(text_buffer, "SCORE: %d\n", score);
		font->print(font_pos, (char*)text_buffer, 1, 1, 1);
		font_pos[1] += 20;
		sprintf_s(text_buffer, "vel: %02.1f\n", player->vel[1]);
		font->print(font_pos, (char*)text_buffer, 1, 1, 1);
		if (current_scene == game_over_scene) {
			sprintf_s(text_buffer, "GAME OVER\n");
			font->print(Vecf{ DEFAULT_SCREEN_WIDTH / 2, DEFAULT_SCREEN_HEIGHT / 2 }, (char*)text_buffer, 1, 1, 1);
		}

	}

	bool last_tunnel_top = false;
	system_clock::time_point tp;
	long tunnel_spawn_time_ms = 1000;
	int min_tunnel_height = 200;
	int max_tunnel_height = 400;
	int body_count = 0;
	int score = 0;
};

static FlappyBird* app;

int main()
{
	printf("Flappy Bird app!\n");
	printf("Arcane version: %s\n", arcane_version_string().c_str());

	app = new FlappyBird();
	app->init();

	Bird* player = new Bird();
	app->player = player;

	Camera* camera = new Camera(0, 0, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);
	Image* background_img = new Image("assets/flappy_bird_background.png");

	Scene* scene = new Scene(camera, background_img, DEFAULT_SCREEN_WIDTH * 2, DEFAULT_SCREEN_HEIGHT * 2);
	scene->gravity = 1;

	scene->add_body(player);

	game_over_scene = new Scene(camera, background_img, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);
	game_over_scene->name = game_over_name;

	EventsManager* ev_manager = EventsManager::getInstance();
	ev_manager->subscribe(EventType::KeyboardInput, player);

	app->events_manager = ev_manager;
	app->current_scene = scene;

	app->run();

	delete app;

}
