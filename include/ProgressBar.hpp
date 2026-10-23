#pragma once

#include "Arcane2DLib.hpp"
#include "Image.hpp"
#include "BodyRectangle.hpp"
#include "UiComponent.hpp"
#include "ArcaneTypes.hpp"

class A2D_API ProgressBar : public UiComponent {
public:
    ProgressBar(Vecf pos, Image* back, Image* front, int min=0, int max=100);
    ProgressBar(std::vector<char> serialized_data);

    virtual std::vector<char> serialize();
    void draw() override;
    //should show min max on mouse_over
    //void mouse_over() override;

    //may do animations
    void set_max(int nmax);
    void set_current(int ncurrent);
    virtual ObjectType get_type() { return ObjectType::ProgressBar; }

    Image* progress_back;
    Image* progress_front;
    int min, max, current, last_current, original_width;
};
