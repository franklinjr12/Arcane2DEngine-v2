// Built with Arcane engine version 0.1.0.1

#include <ArcaneVersion.hpp>
#include <Application.hpp>
#include <StaticBody.hpp>
#include <cstdio>
#include <cstdlib>

const std::string top_wall_name = "top_wall";
const std::string bop_wall_name = "bop_wall";
const std::string player_name = "player";
const std::string enemy_name = "enemy";
const std::string ball_name = "ball";

class Pong : public Application {
public:

	void game_loop() override {
		enemy->vel[0] = player->vel[0];
		enemy->vel[1] = player->vel[1];
	}

	void game_draw() override {

	}

	DynamicBody* enemy;
	DynamicBody* ball;
};

static Pong* app;

class Paddle : public DynamicBody {
public:

	Paddle(Veci initial_pos) {
		const std::string sprite_path = "assets/paddle.png";
		image = new Image(sprite_path);
		rectangle = new BodyRectangle(pos.pos, image->width, image->height);
		setX(initial_pos[0]);
		setY(initial_pos[1]);
	}

	const float paddle_vel = 5;
};

class PlayerPaddle : public Player {
public:

	PlayerPaddle() {
		Veci initial_pos = { 1, DEFAULT_SCREEN_HEIGHT / 2 };
		paddle = new Paddle(initial_pos);
		image = paddle->image;
		rectangle = paddle->rectangle;
		setX(paddle->getX());
		setY(paddle->getY());
	}

	Paddle* paddle;
};

class Ball : public DynamicBody {
public:

	Ball() {
		const std::string sprite_path = "assets/ball10x10.png";
		image = new Image(sprite_path);
		rectangle = new BodyRectangle(pos.pos, image->width, image->height);
		setX(DEFAULT_SCREEN_WIDTH / 2);
		setY(DEFAULT_SCREEN_HEIGHT / 2);
		srand(time(0));
		float xdir = rand() % 6 > 3 ? 1 : -1;
		float ydir = rand() % 6 > 3 ? 1 : -1;
		vel[0] = xdir * ball_vel;
		vel[1] = ydir * ball_vel;
	}

	void handle_collision(ObjectId _id) override {
		std::string obj_name = app->current_scene->get_body(_id)->name;
		if (obj_name == top_wall_name || obj_name == bop_wall_name) {
			vel[1] *= -1.0;
		}
		else if (obj_name == player_name || obj_name == enemy_name) {
			vel[0] *= -1.0;
		}
	}

	const float ball_vel = 7;
};

int main() {

	printf("Pong app!\n");
	printf("Arcane version: %s\n", arcane_version_string().c_str());

	app = new Pong();
	app->init();

	app->player = new PlayerPaddle();
	app->player->name = player_name;
	PlayerPaddle* player = (PlayerPaddle*)app->player;
	app->player->handler.callback = [player](std::vector<event_bytes_type> data) {
		switch (data[0]) {
		case (event_bytes_type)EventType::KeyboardInput:
			if (data[1] == GLFW_PRESS || data[1] == GLFW_REPEAT) {
				switch (data[2]) {
				case GLFW_KEY_UP:
					player->vel[1] = -player->paddle->paddle_vel;
					break;
				case GLFW_KEY_DOWN:
					player->vel[1] = player->paddle->paddle_vel;
					break;
				default:
					break;
				}
			}
			if (data[1] == GLFW_RELEASE) {
				switch (data[2]) {
				case GLFW_KEY_UP:
				case GLFW_KEY_DOWN:
					player->vel[1] = 0;
					break;
				default:
					break;
				}
			}
		}
		};
	app->enemy = new Paddle(Veci{ DEFAULT_SCREEN_WIDTH - 20, DEFAULT_SCREEN_HEIGHT / 2 });
	app->enemy->name = enemy_name;
	app->ball = new Ball();
	app->ball->name = ball_name;
	const int WALL_WIDTH = DEFAULT_SCREEN_WIDTH;
	const int WALL_HEIGHT = 10;
	//Image* wall_image = new Image("assets/wall_test_texture.png", WALL_WIDTH, WALL_HEIGHT);
	Image* wall_image = new Image("assets/blue_rect.png", WALL_WIDTH, WALL_HEIGHT);
	Vecf pos_br1 = { DEFAULT_SCREEN_WIDTH / 2,0 };
	BodyRectangle* br1 = new BodyRectangle(pos_br1, WALL_WIDTH, WALL_HEIGHT);
	StaticBody* top_wall = new StaticBody(wall_image, br1);
	top_wall->name = top_wall_name;
	top_wall->draw_rect_overlay = true;
	Vecf pos_br2 = { DEFAULT_SCREEN_WIDTH / 2,DEFAULT_SCREEN_HEIGHT - wall_image->height };
	BodyRectangle* br2 = new BodyRectangle(pos_br2, WALL_WIDTH, WALL_HEIGHT);
	StaticBody* bop_wall = new StaticBody(wall_image, br2);
	bop_wall->draw_rect_overlay = true;
	bop_wall->name = bop_wall_name;

	Camera* camera = new Camera(0, 0, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);

	Scene* scene = new Scene(camera, nullptr, DEFAULT_SCREEN_WIDTH * 2, DEFAULT_SCREEN_HEIGHT * 2);
	scene->gravity = 0;

	scene->add_body(app->player);
	scene->add_body(app->enemy);
	scene->add_body(app->ball);
	scene->add_body(top_wall);
	scene->add_body(bop_wall);

	EventsManager ev_manager;
	ev_manager.subscribe(EventType::KeyboardInput, app->player->handler);

	app->events_manager = &ev_manager;
	app->current_scene = scene;

	app->run();

	delete app;

	return 0;
}