// No window, no GL, no audio: physics stepping must work headlessly.
#include <box2d/box2d.h>

#include "test_assert.hpp"

int main() {
  b2WorldDef world_def = b2DefaultWorldDef();
  world_def.gravity = {0.0f, -10.0f};
  b2WorldId world = b2CreateWorld(&world_def);

  b2BodyDef body_def = b2DefaultBodyDef();
  body_def.type = b2_dynamicBody;
  body_def.position = {0.0f, 10.0f};
  b2BodyId body = b2CreateBody(world, &body_def);

  b2ShapeDef shape_def = b2DefaultShapeDef();
  b2Polygon box = b2MakeBox(0.5f, 0.5f);
  b2CreatePolygonShape(body, &shape_def, &box);

  for (int i = 0; i < 60; ++i) {
    b2World_Step(world, 1.0f / 60.0f, 4);
  }

  const float y = b2Body_GetPosition(body).y;
  // Free fall for 1s under g=10 drops roughly 5 units: y ~= 10 - 0.5*g*t^2.
  ARCANE_ASSERT_TRUE(y < 10.0f - 4.0f && y > 10.0f - 6.0f);

  b2DestroyWorld(world);
  return 0;
}
