#include <json.hpp>

#include "test_assert.hpp"

int main() {
  nlohmann::json original;
  original["name"] = "sample_scene";
  original["version"] = 1;
  original["position"] = {1.5, -2.5};

  const std::string text = original.dump();
  const nlohmann::json round_tripped = nlohmann::json::parse(text);

  ARCANE_ASSERT_TRUE(round_tripped["name"] == "sample_scene");
  ARCANE_ASSERT_TRUE(round_tripped["version"] == 1);
  ARCANE_ASSERT_TRUE(round_tripped["position"][0] == 1.5);
  ARCANE_ASSERT_TRUE(round_tripped == original);

  return 0;
}
