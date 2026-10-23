#pragma once

#include "Arcane2DLib.hpp"
#include "Image.hpp"
#include "UiComponent.hpp"
#include "Animation.hpp"


// will mainly be used with animation
class A2D_API ImageDisplay : public UiComponent {
public:
    ImageDisplay(Vecf pos, Image* image);
    ImageDisplay(std::vector<char>& serialized_data);

    virtual std::vector<char> serialize();
    void draw() override;
    virtual ObjectType get_type() { return ObjectType::ImageDisplay; }
};
