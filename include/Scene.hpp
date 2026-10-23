#pragma once

#include "Arcane2DLib.hpp"
#include "Camera.hpp"
#include "DynamicBody.hpp"
#include "StaticBody.hpp"
#include "Particle.hpp"
#include "UiComponent.hpp"

#include <vector>
#include <forward_list>
#include <unordered_map>

class A2D_API Scene : public Object {
public:

	Scene(Camera* camera, Image* background, uint32_t w, uint32_t h);
	Scene(std::vector<char>& serialized_data);
	~Scene();

	void draw() override;
	void update(Veci mouse_pos, float delta = 1);
	void resolution_changed(int new_width, int new_height);
	void process_events(std::vector<event_bytes_type> data) override;
	// should implement this if you want something to happen when this scene is loaded
	virtual void on_scene_entered() {}
	virtual void on_scene_exited() {}
	void add_body(Body* body);
	Body* get_body(ObjectId id);
	Body* remove_body(ObjectId id);
	virtual std::vector<char> serialize();
	// this should be used to set a scene initial condition
	void save_initial_scene();
	// this will cause deletion of external objects which is a problem, must be careful when using
	void load_initial_scene();
	bool save_scene_to_file(std::string path = "saved_data/scene");
	bool load_scene_from_file(std::string path = "");

	Camera* camera;
	Image* background;
	uint32_t w, h;
	float gravity = 0.1f;

	std::forward_list<StaticBody*> static_bodies;
	std::forward_list<DynamicBody*> dynamic_bodies;
	std::forward_list<UiComponent*> uis;
	std::unordered_map<ObjectId, Body*> bodies_map;

protected:

	void free_resources();
	void init_from_serialized_data(std::vector<char>& serialized_data);
	std::vector<char> saved_scene_data;
};