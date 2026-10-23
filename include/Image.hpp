#pragma once

#include "Arcane2DLib.hpp"
#include "ArcaneTypes.hpp"

#include <iostream>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <vector>

class A2D_API Image {
public:

    Image(std::string path, int custom_w=0, int custom_h=0, bool texture_clamp = false, bool flipv = true, bool fliph = false);
    Image(std::vector<char>& serialized_data);
    ~Image();
    void loadImage(std::string path, bool flipv = true, bool fliph = false);
    void draw(Vecf pos, int w = 1, int h = 1);
    void draw(Vecf pos, RGBA_t color, float w = 1, float h = 1);
    void drawRotated(Vecf pos, float angle, float w=1, float h=1);
    void resize(int neww, int newh, bool flipv = true, bool fliph = false);
    void flipv();
    void fliph();
    virtual std::vector<char> serialize();
    virtual ObjectType get_type() { return ObjectType::Image; }

    std::string path;
    float width, height;
    GLuint texture_id = 0;
    GLint texture_mode = GL_CLAMP_TO_EDGE;
    bool last_flipv, last_fliph;
};
