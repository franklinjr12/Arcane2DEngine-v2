#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "arcane/log.hpp"
#include "test_assert.hpp"

int main() {
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

  std::filesystem::remove(path);
  return 0;
}
