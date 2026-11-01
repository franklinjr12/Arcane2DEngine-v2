// Crash-atomic multi-file save: failure injection between every step of the journaled commit.
#include <filesystem>
#include <string>

#include "arcane/atomic_save.hpp"
#include "test_assert.hpp"

namespace {

namespace fs = std::filesystem;

std::string slurp(const fs::path& path) {
  auto text = arcane::read_text_file(path);
  ARCANE_ASSERT_TRUE(text.has_value());
  return *text;
}

void no_leftovers(const fs::path& root) {
  ARCANE_ASSERT_TRUE(!fs::exists(root / ".arcane-commit"));
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    ARCANE_ASSERT_TRUE(entry.path().extension() != ".arcane-tmp");
  }
}

struct Fixture {
  fs::path root = fs::temp_directory_path() / "arcane_test_atomic_save";
  fs::path a = root / "project.json", b = root / "scenes" / "main.scene.json", c = root / "new.json";
  std::vector<arcane::FileWrite> files = {{a, "new a"}, {b, "new b"}, {c, "new c"}};

  void reset() {
    fs::remove_all(root);
    fs::create_directories(root / "scenes");
    ARCANE_ASSERT_TRUE(arcane::write_text_file_atomic(a, "old a").has_value());
    ARCANE_ASSERT_TRUE(arcane::write_text_file_atomic(b, "old b").has_value());
  }
  bool all_old() const { return slurp(a) == "old a" && slurp(b) == "old b" && !fs::exists(c); }
  bool all_new() const { return slurp(a) == "new a" && slurp(b) == "new b" && slurp(c) == "new c"; }
};

void test_commit_without_failure() {
  Fixture f;
  f.reset();
  ARCANE_ASSERT_TRUE(arcane::commit_files_atomic(f.root, f.files).has_value());
  ARCANE_ASSERT_TRUE(f.all_new());
  no_leftovers(f.root);
  fs::remove_all(f.root);
}

void test_crash_at_every_step() {
  Fixture f;
  const int steps = arcane::atomic_save_step_count(f.files.size());
  for (int crash = 0; crash < steps; ++crash) {
    f.reset();
    auto result = arcane::commit_files_atomic(f.root, f.files, {.crash_at_step = crash});
    ARCANE_ASSERT_TRUE(!result.has_value() && result.error() == "simulated crash");
    // Disk now looks like a killed process left it; reopening must settle on one whole set.
    ARCANE_ASSERT_TRUE(arcane::recover_pending_commit(f.root).has_value());
    const int commit_point = static_cast<int>(f.files.size());  // the manifest step
    if (crash < commit_point) {
      ARCANE_ASSERT_TRUE(f.all_old());
    } else {
      ARCANE_ASSERT_TRUE(f.all_new());
    }
    no_leftovers(f.root);
  }
  fs::remove_all(f.root);
}

void test_next_save_recovers_crashed_commit() {
  Fixture f;
  f.reset();
  (void)arcane::commit_files_atomic(f.root, f.files, {.crash_at_step = 4});
  const std::vector<arcane::FileWrite> next = {{f.a, "next a"}, {f.b, "next b"}};
  ARCANE_ASSERT_TRUE(arcane::commit_files_atomic(f.root, next).has_value());
  ARCANE_ASSERT_TRUE(slurp(f.a) == "next a" && slurp(f.b) == "next b" && slurp(f.c) == "new c");
  no_leftovers(f.root);
  fs::remove_all(f.root);
}

void test_torn_manifest_is_discarded() {
  Fixture f;
  f.reset();
  (void)arcane::commit_files_atomic(f.root, f.files, {.crash_at_step = 2});
  ARCANE_ASSERT_TRUE(arcane::write_text_file_atomic(f.root / ".arcane-commit", "arcane-commit 1\nproject.json\n").has_value());
  ARCANE_ASSERT_TRUE(arcane::recover_pending_commit(f.root).has_value());
  ARCANE_ASSERT_TRUE(f.all_old());
  no_leftovers(f.root);
  fs::remove_all(f.root);
}

void test_ordinary_failure_rolls_back() {
  Fixture f;
  for (int fail = 0; fail < 3; ++fail) {
    f.reset();
    auto result = arcane::commit_files_atomic(f.root, f.files, {.fail_commit_index = fail});
    ARCANE_ASSERT_TRUE(!result.has_value());
    ARCANE_ASSERT_TRUE(result.error().starts_with("rolled back: simulated failure committing"));
    ARCANE_ASSERT_TRUE(f.all_old());
    no_leftovers(f.root);
  }
  fs::remove_all(f.root);
}

void test_rejects_path_outside_root() {
  Fixture f;
  f.reset();
  auto result = arcane::commit_files_atomic(f.root, {{f.root.parent_path() / "outside.json", "x"}});
  ARCANE_ASSERT_TRUE(!result.has_value());
  fs::remove_all(f.root);
}

}  // namespace

int main() {
  test_commit_without_failure();
  test_crash_at_every_step();
  test_next_save_recovers_crashed_commit();
  test_torn_manifest_is_discarded();
  test_ordinary_failure_rolls_back();
  test_rejects_path_outside_root();
  return 0;
}
