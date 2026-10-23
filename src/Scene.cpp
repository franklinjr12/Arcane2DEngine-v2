#include "Scene.hpp"

#include "ArcaneUtils.hpp"
#include "Collisions.hpp"
#include "Logger.hpp"
#include "StaticBody.hpp"
#include "Button.hpp"
#include "ProgressBar.hpp"
#include "ImageDisplay.hpp"
#include "TextDisplay.hpp"
#include "EventsManager.hpp"

Scene::Scene(Camera* camera, Image* background, uint32_t w, uint32_t h) : camera(camera), background(background), w(w), h(h) {
	EventsManager::getInstance()->subscribe(EventType::MouseInput, this);
}

Scene::~Scene() {
	EventsManager::getInstance()->unsubscribe(EventType::MouseInput, this);
	free_resources();
}

void Scene::update(Veci mouse_pos, float delta) {
				auto bodies_end = dynamic_bodies.end();
    for (auto it = dynamic_bodies.begin(); it != bodies_end; it++) {
        Body* body = *it;
        if (body->suffer_gravity)
            body->update(gravity, delta);
        else
            body->update(0, delta);
        // TODO check if there is a better way to check collisions
        if (body->can_collide) {
            auto inner_bodies_end = dynamic_bodies.end();
            for (auto it_inner = dynamic_bodies.begin(); it_inner != inner_bodies_end; it_inner++) {
                Body* body_inner = *it_inner;
                if (body_inner->can_collide) {
                    if (isRectColliding(*body->rectangle, *body_inner->rectangle)) {
                        if (body != body_inner) { // don't check collision with self
                            body->collided = true;
                            body_inner->collided = true;
                            body->handle_collision(body_inner->id);
                        }
                    }
                }
            }
												auto inner_static_bodies_end = static_bodies.end();
												for (auto it_inner = static_bodies.begin(); it_inner != inner_static_bodies_end; it_inner++) {
																Body* body_inner = *it_inner;
																if (body_inner->can_collide) {
																				if (isRectColliding(*body->rectangle, *body_inner->rectangle)) {
																								if (body != body_inner) { // don't check collision with self
																												body->collided = true;
																												body_inner->collided = true;
																												body->handle_collision(body_inner->id);
																								}
																				}
																}
												}
        }
    }
				auto uis_end = uis.end();
    for (auto it = uis.begin(); it != uis_end; it++) {
        UiComponent* ui = *it;
        ui->update();
        if (ui->should_draw) {
            Point p;
            p.x = mouse_pos[0];
            p.y = mouse_pos[1];
            if (isPointRectColliding(*(ui->rect), p))
                ui->mouse_over();
        }
    }
    _update();
}

void Scene::process_events(std::vector<event_bytes_type> data) {
	switch (data[0]) {
	case (event_bytes_type)EventType::MouseInput:
		if (data[1] == GLFW_PRESS && data[2] == GLFW_MOUSE_BUTTON_1) {
			for (auto it = uis.begin(); it != uis.end(); it++) {
				UiComponent* ui = *it;
				if (ui->should_draw) {
					Point p;
					p.x = data[3];
					p.y = data[4];
					if (isPointRectColliding(*(ui->rect), p))
						ui->on_click();
				}
			}
		}
		break;
	default:
		;
	}
	_process_events(data);
}

void Scene::draw() {
	if (background)
		background->draw(Vecf{ camera->rect.pos.x, camera->rect.pos.y }, background->width, background->height);
	//static bodies draw
	auto static_end = static_bodies.end();
	for (auto it = static_bodies.begin(); it != static_end; it++) {
		Body* body = *it;
		if (isRectColliding(*body->rectangle, camera->rect)) {
			Point p;
			p.x = body->getX();
			p.y = body->getY();
			body->setX(p.x + camera->rect.pos.x);
			body->setY(p.y + camera->rect.pos.y);
			body->draw();
			body->setX(p.x);
			body->setY(p.y);
		}
	}
	//dynamic bodies draw
	auto dynamic_end = dynamic_bodies.end();
	for (auto it = dynamic_bodies.begin(); it != dynamic_end; it++) {
					Body* body = *it;
					if (isRectColliding(*body->rectangle, camera->rect)) {
									Point p;
									p.x = body->getX();
									p.y = body->getY();
									body->setX(p.x + camera->rect.pos.x);
									body->setY(p.y + camera->rect.pos.y);
									body->draw();
									body->setX(p.x);
									body->setY(p.y);
					}
	}
	for (auto it = uis.begin(); it != uis.end(); it++) {
		UiComponent* ui = *it;
		if (ui->should_draw) {
			Point p;
			p.x = ui->pos[0];
			p.y = ui->pos[1];
			ui->pos[0] = p.x + camera->rect.pos.x;
			ui->pos[1] = p.y + camera->rect.pos.y;
			ui->draw();
			ui->pos[0] = p.x;
			ui->pos[1] = p.y;
		}
	}
	_draw();
}

void Scene::resolution_changed(int new_width, int new_height) {
	//float width_ratio = ((float)new_width) / DEFAULT_SCREEN_WIDTH;
	//float height_ratio = ((float)new_height) / DEFAULT_SCREEN_HEIGHT;
	//if (background)
	//	background->resize(background->width * width_ratio, background->height * height_ratio);
	//camera->rect.w = camera->rect.w * width_ratio;
	//camera->rect.h = camera->rect.h * height_ratio;
	//for (auto it = bodies.begin(); it != bodies.end(); it++) {
	//	Body* body = *it;
	//	int w = body->rectangle->w * width_ratio;
	//	int h = body->rectangle->h * height_ratio;
	//	if(w > 0 && h > 0)
	//		body->resize(w, h);
	//}
	//for (auto it = uis.begin(); it != uis.end(); it++) {
	//	UiComponent* ui = *it;
	//	int w = ui->rect->w * width_ratio;
	//	int h = ui->rect->h * height_ratio;
	//	if (w > 0 && h > 0)
	//		ui->resize(ui->rect->w * width_ratio, ui->rect->h * height_ratio);
	//}
}

void Scene::add_body(Body* body) {
	A2D_LOGI("body {} added", body->id);
	if(body->get_type() == ObjectType::DynamicBody)
				dynamic_bodies.push_front((DynamicBody*)body);
	else
					static_bodies.push_front((StaticBody*)body);
	bodies_map[body->id] = body;
}

Body* Scene::get_body(ObjectId id) {
	return bodies_map[id];
}

Body* _remove_body(ObjectId id, std::forward_list<Body*>& bodies, std::unordered_map<ObjectId, Body*>& bodies_map) {
				auto prev = bodies.before_begin();
				auto current = bodies.begin();
				auto end = bodies.end();
				auto map_end = bodies_map.end();

				while (current != end) {
								if ((*current)->id == id) {
												// Body with the specified ID found
												A2D_LOGI("body id {} name {} removed", (*current)->id, (*current)->name);

												// Remove the body from the 'bodies' container
												current = bodies.erase_after(prev);

												// Remove the body from the 'bodies_map' container
												auto it = bodies_map.find(id);
												if (it != map_end) {
																bodies_map.erase(it);
												}

												if (current == end) return nullptr;
												return *current; // Return a pointer to the removed body
								}

								++prev;
								++current;
				}

				// Body with the specified ID not found
				return nullptr;
}

Body* Scene::remove_body(ObjectId id) {
				auto& dynamic_bodies_cast = reinterpret_cast<std::forward_list<Body*>&>(dynamic_bodies);
				Body* b = _remove_body(id, dynamic_bodies_cast, bodies_map);

				if (b == nullptr) {
								auto& static_bodies_cast = reinterpret_cast<std::forward_list<Body*>&>(static_bodies);
								return _remove_body(id, static_bodies_cast, bodies_map);
				}

				return b;
}

void Scene::init_from_serialized_data(std::vector<char>& serialized_data) {
	size_t bytes_size;
	const char* ptr;
	ObjectType ot;

	Object::process_serialized_data(serialized_data);

	w = *reinterpret_cast<const uint32_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(w));
	h = *reinterpret_cast<const uint32_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(h));
	gravity = *reinterpret_cast<const float*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(gravity));

	bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
	auto v = std::vector<char>(serialized_data.begin(), serialized_data.begin() + bytes_size);
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
	camera = new Camera(v);

	bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
	v = std::vector<char>(serialized_data.begin(), serialized_data.begin() + bytes_size);
	serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
	background = new Image(v);

	while (!serialized_data.empty()) {
		ot = *reinterpret_cast<const ObjectType*>(serialized_data.data());
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(ot));
		bytes_size = *reinterpret_cast<const size_t*>(serialized_data.data());
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + sizeof(bytes_size));
		v = std::vector<char>(serialized_data.begin(), serialized_data.begin() + bytes_size);
		serialized_data.erase(serialized_data.begin(), serialized_data.begin() + bytes_size);
		// check if it is really needed to case for types like Object, Image...
		switch (ot) {
		case ObjectType::Body:
		{
			Body* b = new Body(v);
			add_body(b);
			break;
		}
		case ObjectType::DynamicBody:
		{
			auto* d = new DynamicBody(v);
			add_body(d);
			break;
		}
		case ObjectType::StaticBody:
		{
			auto* s = new StaticBody(v);
			add_body(s);
			break;
		}
		case ObjectType::UiComponent:
		{
			auto* u = new UiComponent(v);
			uis.push_front(u);
			break;
		}
		case ObjectType::Button:
		{
			auto* bu = new Button(v);
			uis.push_front(bu);
			break;
		}
		case ObjectType::ProgressBar:
		{
			auto* pu = new ProgressBar(v);
			uis.push_front(pu);
			break;
		}
		case ObjectType::TextDisplay:
		{
			auto* tu = new TextDisplay(v);
			uis.push_front(tu);
			break;
		}
		case ObjectType::ImageDisplay:
		{
			auto* iu = new ImageDisplay(v);
			uis.push_front(iu);
			break;
		}
		}
	}
}

Scene::Scene(std::vector<char>& serialized_data) : Object(serialized_data) {
	init_from_serialized_data(serialized_data);
}

std::vector<char> Scene::serialize() {
	std::vector<char> v;
	size_t bytes_size;
	const char* ptr;
	ObjectType ot;

	auto ser_obj = Object::serialize();
	v.insert(v.end(), ser_obj.begin(), ser_obj.end());

	ptr = reinterpret_cast<const char*>(&w);
	v.insert(v.end(), ptr, ptr + sizeof(w));
	ptr = reinterpret_cast<const char*>(&h);
	v.insert(v.end(), ptr, ptr + sizeof(h));
	ptr = reinterpret_cast<const char*>(&gravity);
	v.insert(v.end(), ptr, ptr + sizeof(gravity));

	auto ser_camera = camera->serialize();
	bytes_size = ser_camera.size();
	ptr = reinterpret_cast<const char*>(&bytes_size);
	v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
	v.insert(v.end(), ser_camera.begin(), ser_camera.end());

	auto ser_back = background->serialize();
	bytes_size = ser_back.size();
	ptr = reinterpret_cast<const char*>(&bytes_size);
	v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
	v.insert(v.end(), ser_back.begin(), ser_back.end());

	for (Object* e : static_bodies) {
		ot = e->get_type();
		ptr = reinterpret_cast<const char*>(&ot);
		v.insert(v.end(), ptr, ptr + sizeof(ot));
		auto ser = e->serialize();
		bytes_size = ser.size();
		ptr = reinterpret_cast<const char*>(&bytes_size);
		v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
		v.insert(v.end(), ser.begin(), ser.end());
	}

	for (Object* e : dynamic_bodies) {
					ot = e->get_type();
					ptr = reinterpret_cast<const char*>(&ot);
					v.insert(v.end(), ptr, ptr + sizeof(ot));
					auto ser = e->serialize();
					bytes_size = ser.size();
					ptr = reinterpret_cast<const char*>(&bytes_size);
					v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
					v.insert(v.end(), ser.begin(), ser.end());
	}

	for (Object* e : uis) {
		ot = e->get_type();
		ptr = reinterpret_cast<const char*>(&ot);
		v.insert(v.end(), ptr, ptr + sizeof(ot));
		auto ser = e->serialize();
		bytes_size = ser.size();
		ptr = reinterpret_cast<const char*>(&bytes_size);
		v.insert(v.end(), ptr, ptr + sizeof(bytes_size));
		v.insert(v.end(), ser.begin(), ser.end());
	}

	return v;
}

void Scene::save_initial_scene() {
	saved_scene_data = serialize();
}

void Scene::free_resources() {
	A2D_LOGI("Freeing scene {}{}", name, (uint64_t)id);
	auto temp_static_bodies = static_bodies;
	for (auto* e : temp_static_bodies)
		remove_body(e->id);
	auto temp_dynamic_bodies = dynamic_bodies;
	for (auto* e : temp_dynamic_bodies)
					remove_body(e->id);
	auto temp_uis = uis;
	for (auto* e : temp_uis)
		remove_body(e->id);
	//if (background)
	//	delete background;
	delete camera;
}

void Scene::load_initial_scene() {
	A2D_LOGI("Loading scene {}{}", name, (uint64_t)id);
	free_resources();
	// must to a copy because the vector is cleared during init
	auto data = saved_scene_data;
	init_from_serialized_data(data);
}

bool Scene::save_scene_to_file(std::string path) {
	if (saved_scene_data.empty())
		save_initial_scene();
	try {
		std::ofstream outfile(path, std::ios::binary);
		if (outfile.is_open()) {
			outfile.write(saved_scene_data.data(), saved_scene_data.size());
			outfile.close();
		}
		else
			throw std::exception("couldn't open file");
	}
	catch (std::exception e) {
		A2D_LOGE("Error saving scene {}{} to file {}", name, (uint64_t)id, path);
		return false;
	}
	return true;
}

bool Scene::load_scene_from_file(std::string path) {
	try {
		std::ifstream infile(path, std::ios::binary | std::ios::ate);
		std::streamsize size = infile.tellg();
		infile.seekg(0, std::ios::beg);
		if (infile.is_open()) {
			saved_scene_data.reserve(size);
			infile.read(saved_scene_data.data(), size);
			infile.close();
		}
		else
			throw std::exception("couldn't open file");
		load_initial_scene();
	}
	catch (std::exception e) {
		A2D_LOGE("Error reading scene from file {}", path);
		return false;
	}
	return true;
}
