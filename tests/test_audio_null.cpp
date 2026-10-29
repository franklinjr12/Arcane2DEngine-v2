// Verify miniaudio's null context can initialize without opening any device.
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include "test_assert.hpp"

int main() {
  ma_backend backends[] = {ma_backend_null};
  ma_context context;
  ARCANE_ASSERT_TRUE(ma_context_init(backends, 1, nullptr, &context) == MA_SUCCESS);
  ma_context_uninit(&context);
  return 0;
}
