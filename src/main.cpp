#include "Application.hpp"
#include "DynamicBody.hpp"
#include "ProgressBar.hpp"
#include "TextDisplay.hpp"
#include "ImageDisplay.hpp"
#include "AssetsManager.hpp"

#include <iostream>
#include <Windows.h>

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
								current = std::chrono::system_clock::now();
								last = std::chrono::system_clock::now();
				}

				void game_loop() override {
								updates++;
								current = std::chrono::system_clock::now();
								auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(current - last).count();
								const auto c = wait_time.count();
								if (diff > c) {
												last = std::chrono::system_clock::now();
												printf("updates: %ld\tdelta: %1.9f\n", updates, delta);
												updates = 0;
								}
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
				std::chrono::system_clock::time_point current;
				std::chrono::system_clock::time_point last;
				const std::chrono::milliseconds wait_time = 100ms;
				long updates = 0;
};

int main2()
{
				printf("BodyTest app\n");

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
				return 0;
}

class GameExample : public Application {
public:

	GameExample() {
		start_btn = nullptr;
		rotatedImage = new Image("assets/main_character.png");
	}
	void game_loop() override {
	}
	void game_draw() override {
		Vecf pos;
		pos[0] = 10;
		pos[1] = 40;
		const int buf_size = 128;
		char text_buffer[buf_size];
		sprintf_s(text_buffer, "mx: %03d my: %03d", (int)mouse_pos[0], (int)mouse_pos[1]);
		font->print(pos, (char*)text_buffer);
		pos[1] += 20;
		sprintf_s(text_buffer, "px: %03d py: %03d", (int)player->getX(), (int)player->getY());
		font->print(pos, (char*)text_buffer);
		pos[1] += 20;
		sprintf_s(text_buffer, "rx: %03d ry: %03d", (int)player->rectangle->pos.x, (int)player->rectangle->pos.x);
		font->print(pos, (char*)text_buffer);
#ifdef DEBUG_SHOW_FPS
		pos[0] = DEFAULT_SCREEN_WIDTH / 2;
		pos[1] = 100;
		sprintf_s(text_buffer, "FPS: %d\n", (int)fc.sleep_time);
		font->print(pos, (char*)text_buffer);
		//delete font;
#endif
		rotatedImage->drawRotated(Vecf{ 300,300 }, 90);
	}
	void process_events(std::vector<event_bytes_type> data) override {
		switch (data[0]) {
		case (event_bytes_type)EventType::ButtonClicked: {
			ObjectId btn_id = (ObjectId)data[1];
						printf("Got button clicked id %lu\n", btn_id);
			if (start_btn != nullptr && btn_id == start_btn->id) {
				if (game_scene != nullptr) {
					current_scene = game_scene;
					font = font_small;
				}
			}
			break;
		}
		case (event_bytes_type)EventType::Timer:
			ObjectId timer_id = (ObjectId)data[1];
			printf("Timer %lu timeout\n", timer_id);
			t1->reset();
			break;
		}
	}

	Font* font;
	Font* font_small;
	Font* font_big;
	Button* start_btn;
	Scene* menu_scene;
	Scene* game_scene;
	Timer* t1;
	Image* rotatedImage;
};

class MyPlayer : public Player {
public:

	MyPlayer() {
		image = new Image("assets/main_character.png");
		Vecf p1 = { DEFAULT_SCREEN_WIDTH / 2 , DEFAULT_SCREEN_HEIGHT / 2 };
		rectangle = new BodyRectangle(p1, image->width, image->height);
		setX(p1[0]);
		setY(p1[1]);
	}

	void process_events(std::vector<event_bytes_type> data) override {
		switch (data[0]) {
		case (event_bytes_type)EventType::KeyboardInput:
			if (data[1] == GLFW_PRESS || data[1] == GLFW_REPEAT) {
				switch (data[2]) {
				case GLFW_KEY_RIGHT:
					vel[0] = 500;
					break;
				case GLFW_KEY_LEFT:
					vel[0] = -500;
					break;

				case GLFW_KEY_DOWN:
					vel[1] = 500;
					break;

				case GLFW_KEY_UP:
					vel[1] = -500;
					break;
				default:
					break;
				}
			}
			else {
				vel[0] = 0;
				vel[1] = 0;
			}
			break;
		case (event_bytes_type)EventType::ButtonClicked:
			A2D_LOGI("got button event!");
			health_ui->set_current(health_ui->current - 10);
			break;
		default:
			break;
		}
	}

	ProgressBar* health_ui;
};


#ifdef _DEBUG
int main()
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
#endif
{
				main2();

	GameExample* app = new GameExample();
	app->t1 = new Timer(2000, false);
	app->t1->start();

	auto asset_man = AssetsManager::get_instance();

	// can only initialize fonts after app->init()
	std::string font_path = "assets/16020_FUTURAM.ttf";
	int font_size = 16;
	Font* font16 = new Font(font_path, font_size);
	asset_man->add_font("font16", font16);
	app->font_small = asset_man->get_font("font16");
	font_size = 64;
	Font* font64 = new Font(font_path, font_size);
	asset_man->add_font("font64", font64);
	app->font_big = asset_man->get_font("font64");
	app->font = app->font_big;

	MyPlayer* player = new MyPlayer();
	player->draw_rect_overlay = true;
	player->vel[0] = 0;

	Camera* camera = new Camera(0, 0, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);
	Image* background = asset_man->get_image("dark_cloud_background1024_720.png");
	Scene* scene = new Scene(camera, background, DEFAULT_SCREEN_WIDTH * 2, DEFAULT_SCREEN_HEIGHT * 2);
	scene->gravity = 0;

	Image* img2 = asset_man->get_image("main_character.png");
	img2->fliph();
	Vecf p2 = { DEFAULT_SCREEN_WIDTH / 2 + 100, DEFAULT_SCREEN_HEIGHT / 2 };
	BodyRectangle* br2 = new BodyRectangle(p2, img2->width, img2->height);
	DynamicBody* b2 = new DynamicBody(img2, br2);
	b2->draw_rect_overlay = true;
	b2->vel[0] = -1;

	player->name = "player name";
	b2->name = "b2 name";
	scene->add_body(player);
	//scene->add_body(b2);

	app->game_scene = scene;
	app->player = player;

	// Ui components

	// button
	int neww = 200, newh = 200;
	//Image btn_img("assets/basic_button.png", neww, newh);
	Image btn_img("assets/basic_button_text.png", neww, newh);
	Vecf button_pos = { (DEFAULT_SCREEN_WIDTH - DEFAULT_SCREEN_WIDTH / 3), 10 };
	Button btn(button_pos, &btn_img, neww, newh);

	// progress bar
	Image* progress_back = asset_man->get_image("progress_bar.png");
	Image* progress_front = asset_man->get_image("progress_bar_front.png");
	Vecf pb_pos;
	pb_pos[0] = 10;
	pb_pos[1] = DEFAULT_SCREEN_HEIGHT - 60;
	ProgressBar* pb = new ProgressBar(pb_pos, progress_back, progress_front);
	player->health_ui = pb;

	// text display
	Image* text_display_image = new Image("assets/basic_button.png");
	Vecf text_pos = { DEFAULT_SCREEN_WIDTH / 2, DEFAULT_SCREEN_HEIGHT - 60 };
	Font* f = AssetsManager::get_instance()->get_default_font();
	std::string t = "T";
	TextDisplay* td = new TextDisplay(text_pos, text_display_image, t, new Font(*f));
	td->font_pos[0] = td->pos[0] + 5;
	td->font_pos[1] = td->pos[1] + td->image->height / 2 + 5;

	// add Ui to scene
	btn.name = "button name";
	pb->name = "progressbar name";
	td->name = "textdisplay name";
	scene->uis.push_front(&btn);
	scene->uis.push_front(pb);
	scene->uis.push_front(td);
	scene->name = "main scene";

	// Menu scene
	Scene* menu_scene = new Scene(new Camera(*camera), new Image(*background), DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);

	//start button
	int start_btn_w = 200, start_btn_h = 200;
	Image btn_start_img("assets/basic_button.png", start_btn_w, start_btn_h);
	Vecf button_start_pos = { DEFAULT_SCREEN_WIDTH / 2 - btn_start_img.width / 2, DEFAULT_SCREEN_HEIGHT / 2 - btn_start_img.height / 2 };
	Font* font_start = AssetsManager::get_instance()->get_default_font();
	Button* btn_start = new Button(button_start_pos, &btn_start_img, start_btn_w, start_btn_h, "START", font_start);
	btn_start->font_pos[0] = btn_start->pos[0] + 20;
	btn_start->font_pos[1] = btn_start->pos[1] + 60;

	menu_scene->uis.push_front(btn_start);
	app->start_btn = btn_start;

	app->menu_scene = menu_scene;
	app->current_scene = menu_scene;

	// events
	app->events_manager->subscribe(EventType::KeyboardInput, player);
	app->events_manager->subscribe(EventType::ButtonClicked, player);
	app->events_manager->subscribe(EventType::ButtonClicked, menu_scene);
	app->events_manager->subscribe(EventType::ButtonClicked, app);
	app->events_manager->subscribe(EventType::Timer, app);

	// infinity loop
	app->run();

	// cleanup
	delete app;

	return 0;
}
