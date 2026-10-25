// Validates the miniaudio null backend initializes and drives a device
// callback without any real audio hardware, so this runs headlessly in CI.
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "test_assert.hpp"

namespace {
std::atomic<unsigned long> g_callback_count = 0;
}

int main() {
  ma_backend backends[] = {ma_backend_null};
  ma_context context;
  ARCANE_ASSERT_TRUE(ma_context_init(backends, 1, nullptr, &context) == MA_SUCCESS);

  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = 2;
  config.sampleRate = 48000;
  config.dataCallback = [](ma_device*, void*, const void*, ma_uint32) { ++g_callback_count; };

  ma_device device;
  ARCANE_ASSERT_TRUE(ma_device_init(&context, &config, &device) == MA_SUCCESS);
  ARCANE_ASSERT_TRUE(ma_device_start(&device) == MA_SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ma_device_uninit(&device);
  ma_context_uninit(&context);

  ARCANE_ASSERT_TRUE(g_callback_count > 0);
  return 0;
}
