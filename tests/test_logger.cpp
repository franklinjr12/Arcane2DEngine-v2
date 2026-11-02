#include <filesystem>
#include <fstream>
#include <vector>
#include <sstream>
#include <string>

#include "arcane/log.hpp"
#include "test_assert.hpp"

namespace {
std::string slurp(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
}  // namespace

int main() {
  using arcane::LogLevel;
  arcane::log_set_stderr_enabled(false);

  ARCANE_ASSERT_TRUE(arcane::to_string(arcane::LogLevel::Info) == "INFO");
  ARCANE_ASSERT_TRUE(arcane::to_string(arcane::LogLevel::Error) == "ERROR");

  const auto path = std::filesystem::temp_directory_path() / "arcane_test_logger.log";
  std::filesystem::remove(path);

  arcane::log_set_file(path);
  arcane::log(arcane::LogLevel::Info, "test", "hello {}", 42);
  arcane::log_set_file("");  // close the file sink before reading it back

  std::ifstream file(path);
  ARCANE_ASSERT_TRUE(file.is_open());
  std::stringstream contents;
  contents << file.rdbuf();
  const std::string text = contents.str();
  file.close();  // Windows can't remove a file that is still open
  ARCANE_ASSERT_TRUE(text.find("INFO") != std::string::npos);
  ARCANE_ASSERT_TRUE(text.find("test") != std::string::npos);
  ARCANE_ASSERT_TRUE(text.find("hello 42") != std::string::npos);

  ARCANE_ASSERT_TRUE(text == "INFO|test|hello 42\n");
  std::filesystem::remove(path);

  // Filtering, per-category override, no output below level (file + custom sink).
  std::vector<std::string> seen;
  const int sink = arcane::log_add_sink([&](LogLevel l, std::string_view c, std::string_view m) {
    seen.push_back(std::string(arcane::to_string(l)) + "/" + std::string(c) + "/" + std::string(m));
  });
  arcane::log_set_file(path);
  arcane::log_set_min_level(LogLevel::Warn);
  ARCANE_ASSERT_TRUE(arcane::log_min_level() == LogLevel::Warn);
  arcane::log(LogLevel::Info, "a", "dropped");
  arcane::log(LogLevel::Warn, "a", "w");
  arcane::log_set_category_level("b", LogLevel::Debug);
  arcane::log(LogLevel::Debug, "b", "kept {}", 1);
  arcane::log(LogLevel::Trace, "b", "dropped");
  arcane::log_set_category_level("c", LogLevel::Error);
  arcane::log(LogLevel::Warn, "c", "dropped");
  ARCANE_ASSERT_TRUE(!arcane::log_would_log(LogLevel::Warn, "c"));
  ARCANE_ASSERT_TRUE(arcane::log_would_log(LogLevel::Warn, "zzz"));
  arcane::log_clear_category_level("b");
  arcane::log(LogLevel::Debug, "b", "dropped");
  arcane::log_remove_sink(sink);
  arcane::log(LogLevel::Error, "a", "after remove");
  arcane::log_set_file("");
  ARCANE_ASSERT_TRUE(seen.size() == 2);
  ARCANE_ASSERT_TRUE(seen[0] == "WARN/a/w");
  ARCANE_ASSERT_TRUE(seen[1] == "DEBUG/b/kept 1");
  ARCANE_ASSERT_TRUE(slurp(path) == "WARN|a|w\nDEBUG|b|kept 1\nERROR|a|after remove\n");
  std::filesystem::remove(path);

  // Rotation: 10-byte cap forces each 9-byte line into its own file.
  arcane::log_set_min_level(LogLevel::Trace);
  arcane::LogFileOptions opt;
  opt.max_bytes = 20;
  opt.max_files = 2;
  arcane::log_set_file(path, opt);
  arcane::log(LogLevel::Info, "r", "1");  // "INFO|r|1\n" = 9 bytes
  arcane::log(LogLevel::Info, "r", "2");
  arcane::log(LogLevel::Info, "r", "3");  // 27 > 20 -> rotates before this line
  arcane::log(LogLevel::Info, "r", "4");
  arcane::log(LogLevel::Info, "r", "5");  // rotates again
  arcane::log_set_file("");
  const auto r1 = std::filesystem::path(path.string() + ".1");
  const auto r2 = std::filesystem::path(path.string() + ".2");
  ARCANE_ASSERT_TRUE(slurp(path) == "INFO|r|5\n");
  ARCANE_ASSERT_TRUE(slurp(r1) == "INFO|r|3\nINFO|r|4\n");
  ARCANE_ASSERT_TRUE(slurp(r2) == "INFO|r|1\nINFO|r|2\n");
  ARCANE_ASSERT_TRUE(!std::filesystem::exists(path.string() + ".3"));
  std::filesystem::remove(path);
  std::filesystem::remove(r1);
  std::filesystem::remove(r2);

  // Optional wall-clock prefix.
  arcane::LogFileOptions ts;
  ts.timestamps = true;
  arcane::log_set_file(path, ts);
  arcane::log(LogLevel::Info, "t", "x");
  arcane::log_set_file("");
  const std::string tl = slurp(path);
  ARCANE_ASSERT_TRUE(tl.size() > std::string("INFO|t|x\n").size() && tl.ends_with("|INFO|t|x\n"));
  std::filesystem::remove(path);
  return 0;
}
