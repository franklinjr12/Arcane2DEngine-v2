// Sprite atlas packing: layout invariants (no overlap, padding, bounds),
// determinism, canonical metadata round trip, UV lookup, folder packing to a
// real PNG, and the atlas.* commands (including clips built from regions).
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <stb_image_write.h>

#include "arcane/animation.hpp"
#include "arcane/atlas.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/migration.hpp"
#include "arcane_editor/project_session.hpp"
#include "arcane_tooling/command.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;
using namespace arcane::tooling;
namespace fs = std::filesystem;

std::string slurp(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_solid_png(const fs::path& path, int w, int h, unsigned char r, unsigned char g, unsigned char b) {
  std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
  for (std::size_t i = 0; i < px.size(); i += 4) {
    px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255;
  }
  fs::create_directories(path.parent_path());
  ARCANE_ASSERT_TRUE(stbi_write_png(path.string().c_str(), w, h, 4, px.data(), w * 4) != 0);
}

void check_layout(const Atlas& atlas, const std::vector<AtlasInput>& inputs) {
  ARCANE_ASSERT_TRUE(atlas.regions.size() == inputs.size());
  for (std::size_t i = 0; i < atlas.regions.size(); ++i) {
    const auto& a = atlas.regions[i];
    const auto in = std::find_if(inputs.begin(), inputs.end(), [&](const AtlasInput& x) { return x.name == a.name; });
    ARCANE_ASSERT_TRUE(in != inputs.end() && in->w == a.w && in->h == a.h);
    ARCANE_ASSERT_TRUE(a.x >= atlas.padding && a.y >= atlas.padding);
    ARCANE_ASSERT_TRUE(a.x + a.w + atlas.padding <= atlas.width && a.y + a.h + atlas.padding <= atlas.height);
    for (std::size_t j = i + 1; j < atlas.regions.size(); ++j) {
      const auto& b = atlas.regions[j];
      const bool apart = a.x + a.w + atlas.padding <= b.x || b.x + b.w + atlas.padding <= a.x ||
                         a.y + a.h + atlas.padding <= b.y || b.y + b.h + atlas.padding <= a.y;
      ARCANE_ASSERT_TRUE(apart);
    }
    if (i > 0) ARCANE_ASSERT_TRUE(atlas.regions[i - 1].name < a.name);
  }
}

void test_layout_invariants() {
  std::vector<AtlasInput> inputs;
  for (int i = 0; i < 40; ++i) inputs.push_back({"s" + std::to_string(100 + i), 5 + (i * 7) % 23, 3 + (i * 11) % 29});
  for (int padding : {0, 1, 3}) {
    auto atlas = pack_atlas_layout(inputs, padding, 512);
    ARCANE_ASSERT_TRUE(atlas.has_value());
    ARCANE_ASSERT_TRUE(atlas->padding == padding && atlas->width <= 512 && atlas->height <= 512);
    check_layout(*atlas, inputs);
    auto reversed = inputs;
    std::reverse(reversed.begin(), reversed.end());
    auto again = pack_atlas_layout(reversed, padding, 512);
    ARCANE_ASSERT_TRUE(again.has_value());
    ARCANE_ASSERT_TRUE(save_atlas_canonical_json(*atlas).value() == save_atlas_canonical_json(*again).value());
  }
  ARCANE_ASSERT_TRUE(!pack_atlas_layout(inputs, 1, 32).has_value());                    // does not fit
  ARCANE_ASSERT_TRUE(!pack_atlas_layout({{"a", 4, 4}, {"a", 4, 4}}, 0, 64).has_value());  // duplicate
  ARCANE_ASSERT_TRUE(!pack_atlas_layout({{"a", 0, 4}}, 0, 64).has_value());
  ARCANE_ASSERT_TRUE(!pack_atlas_layout({}, 0, 64).has_value());
  auto single = pack_atlas_layout({{"only", 16, 16}}, 2, 256);
  ARCANE_ASSERT_TRUE(single.has_value() && single->width == 32 && single->regions[0].x == 2);
}

void test_json_and_uv() {
  auto atlas = pack_atlas_layout({{"a/b", 16, 8}, {"c", 8, 8}}, 0, 64);
  ARCANE_ASSERT_TRUE(atlas.has_value());
  atlas->image = "assets/ui.png";
  auto text = save_atlas_canonical_json(*atlas);
  ARCANE_ASSERT_TRUE(text.has_value());
  ARCANE_ASSERT_TRUE(canonicalize_json(*text).value() == *text);
  auto loaded = load_atlas_from_json(*text);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(save_atlas_canonical_json(*loaded).value() == *text);
  const AtlasRegion* r = find_atlas_region(*loaded, "c");
  ARCANE_ASSERT_TRUE(r != nullptr && find_atlas_region(*loaded, "zz") == nullptr);
  const UvRect uv = atlas_region_uv(*loaded, *r);
  ARCANE_ASSERT_TRUE(uv.u0 == static_cast<float>(r->x) / loaded->width && uv.v1 == static_cast<float>(r->y + r->h) / loaded->height);

  auto bad = [&](const std::string& from, const std::string& to) {
    std::string t = *text;
    const auto at = t.find(from);
    ARCANE_ASSERT_TRUE(at != std::string::npos);
    t.replace(at, from.size(), to);
    return load_atlas_from_json(t).has_value();
  };
  ARCANE_ASSERT_TRUE(!bad("\"padding\"", "\"extra\": 1,\n  \"padding\""));
  ARCANE_ASSERT_TRUE(!bad("\"version\": 2", "\"version\": 3"));
  ARCANE_ASSERT_TRUE(!bad("assets/ui.png", "../ui.png"));
  ARCANE_ASSERT_TRUE(!bad("\"w\": 16", "\"w\": 160"));  // outside the image
  ARCANE_ASSERT_TRUE(!load_atlas_from_json("{\"format_version\": 1}").has_value());
}

struct TempProject {
  fs::path dir = fs::temp_directory_path() / "arcane_atlas_test";
  TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
    ComponentRegistry registry;
    register_builtin_components(registry);
    auto session = editor::ProjectSession::create(dir, "Atlas", registry);
    ARCANE_ASSERT_TRUE(session.has_value());
    ARCANE_ASSERT_TRUE(session->save(registry).has_value());
    write_solid_png(dir / "art/hero/idle_0.png", 16, 24, 255, 0, 0);
    write_solid_png(dir / "art/hero/idle_1.png", 16, 24, 0, 255, 0);
    write_solid_png(dir / "art/coin.png", 8, 8, 0, 0, 255);
    write_solid_png(dir / "art/tree.PNG", 30, 40, 9, 9, 9);
  }
  ~TempProject() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

void test_commands() {
  TempProject project;
  CommandRegistry registry;
  register_atlas_commands(registry);
  auto context = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(context.has_value());
  ProjectContext& ctx = **context;

  Invocation pack{"atlas.pack", {{"folder", "art"}, {"atlas", "assets/art.atlas.json"}, {"padding", 2}}};
  auto packed = registry.execute(ctx, pack);
  if (!packed) std::fprintf(stderr, "pack: %s\n", packed.error().message.c_str());
  ARCANE_ASSERT_TRUE(packed.has_value());
  ARCANE_ASSERT_TRUE(packed->value["image"] == "assets/art.atlas.png");
  ARCANE_ASSERT_TRUE(packed->value["regions"].size() == 4);
  const std::string json1 = slurp(project.dir / "assets/art.atlas.json");
  const std::string png1 = slurp(project.dir / "assets/art.atlas.png");
  ARCANE_ASSERT_TRUE(!png1.empty() && png1.substr(1, 3) == "PNG");

  // Stable output, also when the atlas lives inside the packed folder (its own image is skipped).
  ARCANE_ASSERT_TRUE(registry.execute(ctx, pack).has_value());
  ARCANE_ASSERT_TRUE(slurp(project.dir / "assets/art.atlas.json") == json1);
  ARCANE_ASSERT_TRUE(slurp(project.dir / "assets/art.atlas.png") == png1);
  Invocation inside{"atlas.pack", {{"folder", "art"}, {"atlas", "art/packed.json"}, {"image", "art/packed.png"}}};
  ARCANE_ASSERT_TRUE(registry.execute(ctx, inside).has_value());
  auto inside_again = registry.execute(ctx, inside);
  ARCANE_ASSERT_TRUE(inside_again.has_value() && inside_again->value["regions"].size() == 4);

  auto atlas = load_atlas_from_json(json1);
  ARCANE_ASSERT_TRUE(atlas.has_value() && find_atlas_region(*atlas, "hero/idle_1") && find_atlas_region(*atlas, "tree"));
  int w = 0, h = 0, c = 0;
  ARCANE_ASSERT_TRUE(stbi_info((project.dir / "assets/art.atlas.png").string().c_str(), &w, &h, &c) != 0);
  ARCANE_ASSERT_TRUE(w == atlas->width && h == atlas->height);

  auto one = registry.execute(ctx, {"atlas.regions", {{"atlas", "assets/art.atlas.json"}, {"name", "coin"}}});
  ARCANE_ASSERT_TRUE(one.has_value() && one->value["regions"].size() == 1 && one->value["regions"][0]["uv"].size() == 4);
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"atlas.regions", {{"atlas", "assets/art.atlas.json"}, {"name", "nope"}}}).has_value());

  Invocation make{"atlas.make_clip",
                  {{"atlas", "assets/art.atlas.json"},
                   {"regions", Json::array({"hero/idle_0", "hero/idle_1"})},
                   {"clip", "assets/idle.json"},
                   {"name", "idle"},
                   {"frame_seconds", 0.25}}};
  ARCANE_ASSERT_TRUE(registry.execute(ctx, make).has_value());
  auto clip = load_clip_from_json_strict(slurp(project.dir / "assets/idle.json"));
  ARCANE_ASSERT_TRUE(clip.has_value() && clip->frames.size() == 2 && clip->frames[1].duration_seconds == 0.25f);
  const UvRect want = atlas_region_uv(*atlas, *find_atlas_region(*atlas, "hero/idle_1"));
  ARCANE_ASSERT_TRUE(clip->frames[1].uv.u0 == want.u0 && clip->frames[1].uv.v1 == want.v1);
  ARCANE_ASSERT_TRUE(registry.execute(ctx, make).error().code == "CONFLICT");
  make.args["regions"] = Json::array({"missing"});
  make.args["overwrite"] = true;
  ARCANE_ASSERT_TRUE(registry.execute(ctx, make).error().code == "VALUE_INVALID");

  // Bad arguments write nothing.
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"atlas.pack", {{"folder", "../art"}, {"atlas", "x.json"}}}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"atlas.pack", {{"folder", "nodir"}, {"atlas", "x.json"}}}).has_value());
  ARCANE_ASSERT_TRUE(!fs::exists(project.dir / "x.json"));
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"atlas.pack", {{"folder", "art"}, {"atlas", "x.json"}, {"padding", -1}}}).has_value());
  ARCANE_ASSERT_TRUE(!registry.execute(ctx, {"atlas.pack", {{"folder", "art"}, {"atlas", "x.json"}, {"max_size", 8}}}).has_value());
}

// Sprite.atlas / Sprite.region are registered, settable through prop.set and survive a save.
void test_sprite_region_property() {
  TempProject project;
  CommandRegistry registry;
  register_all_commands(registry);
  auto context = ProjectContext::open(project.dir);
  ARCANE_ASSERT_TRUE(context.has_value());
  ProjectContext& ctx = **context;

  auto node = registry.execute(ctx, {"node.add", {{"name", "Hero"}}});
  ARCANE_ASSERT_TRUE(node.has_value());
  const std::string sel = "id:" + std::to_string(node->value["id"].get<long long>());
  ARCANE_ASSERT_TRUE(registry.execute(ctx, {"component.add", {{"node", sel}, {"type", "Sprite"}}}).has_value());
  auto set = registry.execute(ctx, {"prop.set", {{"node", sel}, {"component", "Sprite"}, {"name", "region"},
                                                  {"value", "hero/idle_0"}}});
  ARCANE_ASSERT_TRUE(set.has_value());
  ARCANE_ASSERT_TRUE(registry.execute(ctx, {"prop.set", {{"node", sel}, {"component", "Sprite"}, {"name", "atlas"},
                                                          {"value", 7}}}).has_value());
  ARCANE_ASSERT_TRUE(ctx.save().has_value());
  const std::string text = slurp(project.dir / "scenes/main.scene.json");
  ARCANE_ASSERT_TRUE(text.find("\"region\": \"hero/idle_0\"") != std::string::npos);
  ARCANE_ASSERT_TRUE(text.find("\"atlas\": 7") != std::string::npos);
}

}  // namespace

int main() {
  test_layout_invariants();
  test_json_and_uv();
  test_commands();
  test_sprite_region_property();
  std::puts("test_atlas OK");
  return 0;
}
