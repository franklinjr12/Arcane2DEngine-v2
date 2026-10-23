// Built with Arcane engine version 0.3.0.0

#include <ArcaneVersion.hpp>
#include <Application.hpp>
#include <FontsManager.hpp>

#include <iostream>
#include <chrono>
#include <cstdlib>

using namespace std::chrono;

std::string ball_name = "ball";
std::string wall_name = "wall";

static Application* app;

class CollisionBall : public DynamicBody {
public:

	CollisionBall(Vecf start) {
		image = new Image("assets/ball10x10.png");
		rectangle = new BodyRectangle(start, image->width, image->height);
		setX(start[0]);
		setY(start[1]);
		int s;
		s = rand() % 10 > 6 ? 1 : -1;
		vel[0] = s * (rand() % max_vel + 1);
		s = rand() % 10 > 6 ? 1 : -1;
		vel[1] = s * (rand() % max_vel + 1);
	}

	void handle_collision(ObjectId _id) override {
		Body* b = app->current_scene->get_body(_id);
		if (b->name == "wt" || b->name == "wb")
			vel[1] = -vel[1];
		else if (b->name == "wl" || b->name == "wr")
			vel[0] = -vel[0];
		else {
			if (b->getX() >= getX() ||
				b->getX() <= getX())
				vel[0] = -vel[0];
			if (b->getY() >= getY() ||
				b->getY() <= getY())
				vel[1] = -vel[1];
		}
	}

	const int max_vel = 500;
};

class Wall : public StaticBody {
public:

	Wall(Vecf wpos, int width, int height) {
		image = new Image("assets/wall_test_texture.png", width, height);
		rectangle = new BodyRectangle(wpos, image->width, image->height);
		setX(wpos[0]);
		setY(wpos[1]);
	}
};

class BodiesTest : public Application {
public:

	BodiesTest() {
		auto now = system_clock::now();
		srand(now.time_since_epoch().count());
	}

	void game_loop() override {
	}

	void game_draw() override {
		static const int buf_size = 128;
		char text_buffer[buf_size];
		Font* font = FontsManager::get_instance()->default_font;
		Vecf font_pos;
		font_pos[0] = 10;
		font_pos[1] = 90;
		sprintf_s(text_buffer, "Bodies: %d\n", body_count);
		font->print(font_pos, (char*)text_buffer, 1, 1, 1);
		font_pos[0] = 300;
		sprintf_s(text_buffer, "FPS: %ld\n", fc.real_fps);
		font->print(font_pos, (char*)text_buffer, 1, 1, 1);
	}

	void process_events(std::vector<event_bytes_type> data) {
		if (data[0] == (event_bytes_type)EventType::MouseInput) {
			if (data[1] == GLFW_PRESS && data[2] == GLFW_MOUSE_BUTTON_1) {
				Vecf p;
				p[0] = data[3];
				p[1] = data[4];
				const int NUM_BALLS = 10;
				for (int i = 0; i < NUM_BALLS; i++) {
								p[0] = 20 + rand() % 550;
								p[1] = 130 + rand() % (660 - 130);
								current_scene->add_body(new CollisionBall(p));
								body_count++;
				}
			}
		}
	}

	int body_count = 0;
};

int main()
{
	printf("BodyTest app\n");
	printf("Arcane version: %s\n", arcane_version_string().c_str());

	app = new BodiesTest();

	Camera* camera = new Camera(0, 0, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);
	Image* background_img = new Image("assets/flappy_bird_background.png");

	Scene* scene = new Scene(camera, background_img, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);

	auto wt = new Wall(Vecf{ 10,100 }, DEFAULT_SCREEN_WIDTH - 80, 20);
	wt->name = "wt";
	scene->add_body(wt);
	auto wb = new Wall(Vecf{ 10,DEFAULT_SCREEN_HEIGHT - 100 }, DEFAULT_SCREEN_WIDTH - 80, 20);
	wb->name = "wb";
	scene->add_body(wb);
	auto wl = new Wall(Vecf{ 10,380 }, 20, DEFAULT_SCREEN_HEIGHT - 190);
	wl->name = "wl";
	scene->add_body(wl);
	auto wr = new Wall(Vecf{ 600, 380 }, 20, DEFAULT_SCREEN_HEIGHT - 190);
	wr->name = "wr";
	scene->add_body(wr);

	EventsManager* ev_manager = EventsManager::getInstance();
	ev_manager->subscribe(EventType::MouseInput, app);

	app->events_manager = ev_manager;
	app->current_scene = scene;

	app->run();

	delete app;

}
