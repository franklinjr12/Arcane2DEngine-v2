// arcane.material v2: round trip, canonical bytes and rejection of bad input.
#include <string>

#include "arcane/material.hpp"
#include "arcane/renderer.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;

std::string doc(const std::string& shader, const std::string& params, const std::string& extra = "",
                const std::string& version = "2") {
  return "{\"format\":\"arcane.material\",\"version\":" + version + ",\"shader\":\"" + shader + "\",\"params\":" + params +
         extra + "}";
}

bool rejects(const std::string& text) { return !load_material_from_json(text).has_value(); }

}  // namespace

int main() {
  const std::string good = doc("assets/glow.frag", R"([{"name":"strength","value":[0.75]},{"name":"glow","value":[1,0.5,0.25]}])",
                               R"(,"blend":"additive")");
  auto loaded = load_material_from_json(good);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->blend == MaterialBlend::Additive);
  ARCANE_ASSERT_TRUE(loaded->params.size() == 2 && loaded->params[0].name == "glow");

  const std::string preamble = material_shader_preamble(*loaded);
  ARCANE_ASSERT_TRUE(preamble.starts_with("#version 330 core\n"));
  ARCANE_ASSERT_TRUE(preamble.find("uniform vec3 glow;\n") != std::string::npos);
  ARCANE_ASSERT_TRUE(preamble.find("uniform float strength;\n") != std::string::npos);
  ARCANE_ASSERT_TRUE(preamble.find("uniform vec2 u_resolution;\n") != std::string::npos);

  auto saved = save_material_canonical_json(*loaded);
  ARCANE_ASSERT_TRUE(saved.has_value());
  const std::string expected =
      "{\n"
      "  \"blend\": \"additive\",\n"
      "  \"format\": \"arcane.material\",\n"
      "  \"params\": [\n"
      "    {\n"
      "      \"name\": \"glow\",\n"
      "      \"value\": [\n"
      "        1.0,\n"
      "        0.5,\n"
      "        0.25\n"
      "      ]\n"
      "    },\n"
      "    {\n"
      "      \"name\": \"strength\",\n"
      "      \"value\": [\n"
      "        0.75\n"
      "      ]\n"
      "    }\n"
      "  ],\n"
      "  \"shader\": \"assets/glow.frag\",\n"
      "  \"version\": 2\n"
      "}\n";
  ARCANE_ASSERT_TRUE(*saved == expected);
  auto again = load_material_from_json(*saved);
  ARCANE_ASSERT_TRUE(again.has_value() && *again == *loaded);

  auto minimal = load_material_from_json(doc("a.frag", "[]"));
  ARCANE_ASSERT_TRUE(minimal.has_value() && minimal->blend == MaterialBlend::Alpha);

  ARCANE_ASSERT_TRUE(rejects(doc("a.frag", "[]", R"(,"extra":1)")));
  ARCANE_ASSERT_TRUE(rejects(doc("a.frag", "[]", "", "1")));
  ARCANE_ASSERT_TRUE(rejects(doc("/abs/a.frag", "[]")));
  ARCANE_ASSERT_TRUE(rejects(doc("a.glsl", "[]")));
  ARCANE_ASSERT_TRUE(rejects(doc("a.frag", R"([{"name":"u_x","value":[1]}])")));
  ARCANE_ASSERT_TRUE(rejects(doc("a.frag", R"([{"name":"gl_x","value":[1]}])")));
  ARCANE_ASSERT_TRUE(rejects(doc("a.frag", R"([{"name":"1x","value":[1]}])")));
  ARCANE_ASSERT_TRUE(rejects(doc("a.frag", R"([{"name":"x","value":[]}])")));
  ARCANE_ASSERT_TRUE(rejects(doc("a.frag", R"([{"name":"x","value":[1,2,3,4,5]}])")));
  ARCANE_ASSERT_TRUE(rejects(doc("a.frag", R"([{"name":"x","value":[1]},{"name":"x","value":[2]}])")));
  ARCANE_ASSERT_TRUE(rejects(doc("a.frag", "[]", R"(,"blend":"multiply")")));

  Material bad;
  bad.shader = "a.frag";
  bad.params = {{"x", {1.0f}}, {"x", {2.0f}}};
  ARCANE_ASSERT_TRUE(!save_material_canonical_json(bad).has_value());
  return 0;
}
