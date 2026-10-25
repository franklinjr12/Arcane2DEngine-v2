#include "arcane/module.hpp"
#include "test_assert.hpp"

int main() {
  auto good = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(good.has_value());
  ARCANE_ASSERT_TRUE(good->api() != nullptr);
  ARCANE_ASSERT_TRUE(good->api()->api_version == arcane::kGameApiVersion);

  auto bad = arcane::GameModule::load(ARCANE_BAD_VERSION_MODULE_PATH);
  ARCANE_ASSERT_TRUE(!bad.has_value());

  auto missing = arcane::GameModule::load("/nonexistent/path/not_a_module.so");
  ARCANE_ASSERT_TRUE(!missing.has_value());

  return 0;
}
