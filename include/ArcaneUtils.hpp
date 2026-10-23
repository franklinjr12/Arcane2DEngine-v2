#pragma once

#include "Image.hpp"
#include "Arcane2DLib.hpp"

// all game images are based on this resolution
//#define DEFAULT_SCREEN_WIDTH 1920
//#define DEFAULT_SCREEN_HEIGHT 1080
#define DEFAULT_SCREEN_WIDTH 1280
#define DEFAULT_SCREEN_HEIGHT 768

#define DEFAULT_FPS 60

const float OVERLAY_ALPHA = 0.2;

A2D_API Image& GetBlueOverlayImage();

A2D_API float DistanceVecf(Vecf v1, Vecf v2);

A2D_API float MagVecf(Vecf v);

A2D_API bool VectorHasGroupId(std::vector<ObjectGroup>& groups, ObjectGroup& id);
