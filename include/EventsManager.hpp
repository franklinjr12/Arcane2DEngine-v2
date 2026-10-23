#pragma once

#include "Arcane2DLib.hpp"
#include "EventType.hpp"
#include "Object.hpp"

#include <queue>
#include <unordered_map>
#include <vector>

class A2D_API EventsManager {
public:
    static EventsManager* getInstance() {
        static EventsManager instance; // Create a single instance on first access
        return &instance;
    }

    void run();
    void subscribe(EventType evt, Object* obj);
    void unsubscribe(EventType evt, Object* obj);
    void subscribe(std::string evt_name, Object* obj);
    void unsubscribe(std::string evt_name, Object* obj);
    std::queue<EventData> events_data;
    std::queue<CustomEventData> custom_events_data;
    std::unordered_map<std::string, std::vector<Object*>> custom_subscribers;
    std::unordered_map<EventType, std::vector<Object*>> subscribers;

private:
    EventsManager() {} // Private constructor to prevent external instantiation
    EventsManager(const EventsManager&) = delete; // Disable copy constructor
    EventsManager& operator=(const EventsManager&) = delete; // Disable copy assignment
};
