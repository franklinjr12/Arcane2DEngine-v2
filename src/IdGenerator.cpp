#include "IdGenerator.hpp"

ObjectId generate_id() {
    static long long counter = 0;
    long long t = std::chrono::system_clock::now().time_since_epoch().count();
    return static_cast<ObjectId>(t + (counter++));
}