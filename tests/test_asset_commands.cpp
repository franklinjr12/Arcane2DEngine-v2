// asset.list / asset.import / asset.reimport through run_cli on a temporary project: path-derived ids, conflict
// handling, failure injection (every failure leaves the project tree byte-identical, with no *.arcane-tmp), the
// transaction refusal in batches, and hot reload of a reimported clip through editor::AnimationClipCache.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "arcane/animation.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_editor/scene_render.hpp"
#include "arcane_tooling/offline.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;
using namespace arcane::tooling;
namespace fs = std::filesystem;

struct Out {
  int code = -1;
  Json json;
  std::string error_code() const { return json.contains("error") ? json["error"]["code"].get<std::string>() : ""; }
};

Out cli(std::vector<std::string> args, const std::string& stdin_text = {}) {
  std::istringstream in(stdin_text);
  std::ostringstream out, err;
  Out result;
  result.code = run_cli(args, in, out, err);
  result.json = Json::parse(out.str(), nullptr, false);
  return result;
}

std::string slurp(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void spit(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << bytes;
}

// Every regular file below `dir` with its bytes, to compare before and after a failed command.
std::map<std::string, std::string> snapshot(const fs::path& dir) {
  std::map<std::string, std::string> files;
  for (const auto& entry : fs::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file()) files[fs::relative(entry.path(), dir).generic_string()] = slurp(entry.path());
  }
  return files;
}

bool has_tmp(const std::map<std::string, std::string>& files) {
  for (const auto& [path, bytes] : files) {
    if (path.find(".arcane-tmp") != std::string::npos) return true;
  }
  return false;
}

std::string be32(std::uint32_t v) {
  return std::string{static_cast<char>(v >> 24), static_cast<char>(v >> 16), static_cast<char>(v >> 8), static_cast<char>(v)};
}

// Signature + IHDR is all the importer looks at; the rest is not a decodable image.
std::string png_bytes(std::uint32_t w, std::uint32_t h, char filler = 'x') {
  return std::string("\x89PNG\r\n\x1a\n", 8) + be32(13) + "IHDR" + be32(w) + be32(h) + std::string(16, filler);
}

std::string wav_bytes() { return std::string("RIFF\x24\0\0\0WAVEfmt ", 16) + std::string(20, '\0'); }

std::string clip_json(std::size_t frames) {
  AnimationClip clip;
  clip.name = "walk";
  for (std::size_t i = 0; i < frames; ++i) clip.frames.push_back({.uv = {0, 0, 0.5f, 1}, .duration_seconds = 0.1f});
  return save_clip_to_json(clip);
}

struct Fixture {
  fs::path root, project, src;
  Fixture() : root(fs::temp_directory_path() / "arcane_test_asset_commands") {
    std::error_code ec;
    fs::remove_all(root, ec);
    project = root / "game";
    src = root / "src";
    fs::create_directories(src);
    ARCANE_ASSERT_TRUE(cli({"project", "new", project.string(), "--name", "Assets"}).code == 0);
    spit(src / "hero.png", png_bytes(8, 4));
    spit(src / "hero2.png", png_bytes(8, 4, 'y'));
    spit(src / "bad.png", "this is not a png");
    spit(src / "zero.png", png_bytes(0, 4));
    spit(src / "blip.wav", wav_bytes());
    spit(src / "walk1.json", clip_json(1));
    spit(src / "walk2.json", clip_json(2));
    spit(src / "notes.txt", "hello");
    fs::create_directories(src / "folder.png");
  }
  ~Fixture() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
  Out run(std::vector<std::string> words) const {
    std::vector<std::string> args = {"--project", project.string()};
    args.insert(args.end(), words.begin(), words.end());
    return cli(args);
  }
  std::string s(const char* name) const { return (src / name).string(); }
};

void test_import_and_list() {
  Fixture f;
  ARCANE_ASSERT_TRUE(f.run({"asset", "list"}).json["assets"].empty());
  auto png = f.run({"asset", "import", f.s("hero.png"), "sprites/hero.png"});
  ARCANE_ASSERT_TRUE(png.code == 0 && png.json["kind"] == "image" && png.json["replaced"] == false);
  ARCANE_ASSERT_TRUE(f.run({"asset", "import", f.s("blip.wav")}).code == 0);  // dest defaults to the source filename
  ARCANE_ASSERT_TRUE(f.run({"asset", "import", f.s("walk1.json"), "--dest", "walk.json"}).code == 0);
  ARCANE_ASSERT_TRUE(slurp(f.project / "assets/sprites/hero.png") == png_bytes(8, 4));

  auto all = f.run({"asset", "list"});
  ARCANE_ASSERT_TRUE(all.code == 0 && all.json["assets"].size() == 3);
  const char* order[] = {"blip.wav", "sprites/hero.png", "walk.json"};
  const char* kinds[] = {"audio", "image", "clip"};
  for (std::size_t i = 0; i < 3; ++i) {
    const Json& a = all.json["assets"][i];
    ARCANE_ASSERT_TRUE(a["path"] == order[i] && a["kind"] == kinds[i]);
    ARCANE_ASSERT_TRUE(a["id"].is_number_integer() && a["id"].get<AssetId>() == editor::asset_id_for_relative_path(order[i]));
    ARCANE_ASSERT_TRUE(a["bytes"].get<std::size_t>() == fs::file_size(f.project / "assets" / order[i]));
  }
  ARCANE_ASSERT_TRUE(png.json["id"] == all.json["assets"][1]["id"]);
  auto images = f.run({"asset", "list", "--kind", "image"});
  ARCANE_ASSERT_TRUE(images.json["assets"].size() == 1 && images.json["assets"][0]["path"] == "sprites/hero.png");

  // Second import: CONFLICT unless overwrite.
  auto before = snapshot(f.project);
  auto again = f.run({"asset", "import", f.s("hero2.png"), "sprites/hero.png"});
  ARCANE_ASSERT_TRUE(again.code == 5 && again.error_code() == "CONFLICT" && snapshot(f.project) == before);
  auto over = f.run({"asset", "import", f.s("hero2.png"), "sprites/hero.png", "--overwrite", "true"});
  ARCANE_ASSERT_TRUE(over.code == 0 && over.json["replaced"] == true && over.json["id"] == png.json["id"]);
  ARCANE_ASSERT_TRUE(slurp(f.project / "assets/sprites/hero.png") == png_bytes(8, 4, 'y') && !has_tmp(snapshot(f.project)));
}

void test_import_failures_change_nothing() {
  Fixture f;
  ARCANE_ASSERT_TRUE(f.run({"asset", "import", f.s("hero.png")}).code == 0);
  const auto before = snapshot(f.project);
  struct Case {
    std::vector<std::string> words;
    int code;
  };
  const Case cases[] = {
      {{"asset", "import", f.s("bad.png")}, 4},                            // garbage image
      {{"asset", "import", f.s("zero.png")}, 4},                           // zero-sized PNG
      {{"asset", "import", f.s("bad.png"), "hero.png", "--overwrite", "true"}, 4},  // garbage never replaces good bytes
      {{"asset", "import", f.s("notes.txt")}, 2},                          // unknown extension
      {{"asset", "import", f.s("hero.png"), "../escape.png"}, 2},
      {{"asset", "import", f.s("hero.png"), "/abs/escape.png"}, 2},
      {{"asset", "import", f.s("hero.png"), "a\\b.png"}, 2},
      {{"asset", "import", f.s("hero.png"), "C:/x.png"}, 2},
      {{"asset", "import", f.s("hero.png"), "hero.wav"}, 2},               // kind mismatch
      {{"asset", "import", f.s("missing.png")}, 3},
      {{"asset", "import", (f.src / "folder.png").string(), "dir.png"}, 3},                 // a directory is not a source
      {{"asset", "reimport", "nope.png"}, 3},
      {{"asset", "reimport", "hero.png", "--source", f.s("bad.png")}, 4},  // invalid source keeps the old bytes
      {{"asset", "reimport", "hero.png", "--source", f.s("blip.wav")}, 2},
  };
  for (const Case& c : cases) {
    const auto out = f.run(c.words);
    if (out.code != c.code) std::fprintf(stderr, "%s %s -> %d (%s)\n", c.words[1].c_str(), c.words[2].c_str(), out.code, out.error_code().c_str());
    ARCANE_ASSERT_TRUE(out.code == c.code);
    const auto after = snapshot(f.project);
    ARCANE_ASSERT_TRUE(after == before && !has_tmp(after));
  }
  fs::remove_all(f.project / "assets");
  ARCANE_ASSERT_TRUE(f.run({"asset", "import", f.s("hero.png"), "--dry-run"}).code == 0);
  ARCANE_ASSERT_TRUE(!fs::exists(f.project / "assets/hero.png"));
  fs::remove(f.project / "project.json");
  ARCANE_ASSERT_TRUE(f.run({"asset", "list"}).code == 3 && f.run({"asset", "import", f.s("hero.png")}).error_code() == "PROJECT_NOT_FOUND");
}

void test_reimport() {
  Fixture f;
  ARCANE_ASSERT_TRUE(f.run({"asset", "import", f.s("walk1.json"), "walk.json"}).code == 0);
  auto same = f.run({"asset", "reimport", "walk.json"});
  ARCANE_ASSERT_TRUE(same.code == 0 && same.json["changed"] == false);
  auto dry = f.run({"asset", "reimport", "walk.json", "--source", f.s("walk2.json"), "--dry-run"});
  ARCANE_ASSERT_TRUE(dry.code == 0 && dry.json["changed"] == true && slurp(f.project / "assets/walk.json") == clip_json(1));

  // A corrupt file in place is reported and left alone.
  spit(f.project / "assets/broken.png", "garbage");
  const auto before = snapshot(f.project);
  ARCANE_ASSERT_TRUE(f.run({"asset", "reimport", "broken.png"}).code == 4 && snapshot(f.project) == before);

  auto unchanged = f.run({"asset", "reimport", "walk.json", "--source", f.s("walk1.json")});
  ARCANE_ASSERT_TRUE(unchanged.code == 0 && unchanged.json["changed"] == false);
}

void test_hot_reload() {
  Fixture f;
  ARCANE_ASSERT_TRUE(f.run({"asset", "import", f.s("walk1.json"), "walk.json"}).code == 0);
  const AssetId id = editor::asset_id_for_relative_path("walk.json");
  editor::AnimationClipCache cache;
  cache.set_assets(editor::list_animation_clip_assets(f.project / "assets"));
  ARCANE_ASSERT_TRUE(cache.get(id) && cache.get(id)->frames.size() == 1);
  ARCANE_ASSERT_TRUE(cache.refresh() == 0);

  auto re = f.run({"asset", "reimport", "walk.json", "--source", f.s("walk2.json")});
  ARCANE_ASSERT_TRUE(re.code == 0 && re.json["changed"] == true && re.json["id"].get<AssetId>() == id);
  // Force a distinct mtime even on coarse-resolution filesystems.
  fs::last_write_time(f.project / "assets/walk.json", fs::file_time_type::clock::now() + std::chrono::seconds(100));
  ARCANE_ASSERT_TRUE(cache.refresh() == 1);
  ARCANE_ASSERT_TRUE(cache.get(id) && cache.get(id)->frames.size() == 2);
  ARCANE_ASSERT_TRUE(!has_tmp(snapshot(f.project)));
}

void test_batch_rejects_import() {
  Fixture f;
  const std::string script = "{\"command\":\"asset.list\"}\n{\"command\":\"asset.import\",\"args\":{\"source\":\"" + f.s("hero.png") + "\"}}\n";
  const auto before = snapshot(f.project);
  auto out = cli({"--project", f.project.string(), "batch", "-"}, script);
  ARCANE_ASSERT_TRUE(out.code == 7 && out.error_code() == "TRANSACTION_UNSUPPORTED");
  ARCANE_ASSERT_TRUE(snapshot(f.project) == before && !fs::exists(f.project / "assets/hero.png"));
  auto listing = cli({"--project", f.project.string(), "batch", "-"}, "{\"command\":\"asset.list\"}\n");
  ARCANE_ASSERT_TRUE(listing.code == 0);
}

}  // namespace

int main() {
  test_import_and_list();
  test_import_failures_change_nothing();
  test_reimport();
  test_hot_reload();
  test_batch_rejects_import();
  std::puts("test_asset_commands OK");
  return 0;
}
