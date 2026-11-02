#include "arcane/atomic_save.hpp"

#include <cstdio>
#include <format>
#include <optional>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace arcane {

namespace {

namespace fs = std::filesystem;

constexpr const char* kManifestName = ".arcane-commit";
constexpr const char* kManifestHeader = "arcane-commit 1";
constexpr const char* kManifestEnd = "end";
constexpr const char* kTempSuffix = ".arcane-tmp";

struct Crash {};  // thrown by the crash_at_step hook; never escapes this file

// Writes `text` to `path` and flushes it to stable storage before returning.
std::expected<void, std::string> write_synced(const fs::path& path, const std::string& text) {
  std::FILE* file = std::fopen(path.string().c_str(), "wb");
  if (!file) return std::unexpected(std::format("failed to open '{}' for writing", path.string()));
  bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
  ok = std::fflush(file) == 0 && ok;
#ifdef _WIN32
  ok = _commit(_fileno(file)) == 0 && ok;
#else
  ok = fsync(fileno(file)) == 0 && ok;
#endif
  ok = std::fclose(file) == 0 && ok;  // closed before any rename or removal (Windows)
  if (!ok) return std::unexpected(std::format("failed writing '{}'", path.string()));
  return {};
}

void sync_directory(const fs::path& dir) {
#ifndef _WIN32
  int fd = ::open(dir.string().c_str(), O_RDONLY);
  if (fd >= 0) {
    (void)fsync(fd);
    ::close(fd);
  }
#else
  (void)dir;
#endif
}

fs::path temp_of(const fs::path& path) {
  fs::path temp = path;
  temp += kTempSuffix;
  return temp;
}

void remove_quietly(const fs::path& path) {
  std::error_code ec;
  fs::remove(path, ec);
}

// Parses a manifest; nullopt when it is incomplete or malformed (i.e. not committed).
std::optional<std::vector<std::string>> read_manifest(const fs::path& manifest) {
  auto text = read_text_file(manifest);
  if (!text) return std::nullopt;
  std::istringstream in(*text);
  std::string line;
  if (!std::getline(in, line) || line != kManifestHeader) return std::nullopt;
  std::vector<std::string> entries;
  while (std::getline(in, line)) {
    if (line == kManifestEnd) return entries;
    if (line.empty()) return std::nullopt;
    entries.push_back(line);
  }
  return std::nullopt;
}

void remove_stale_temps(const fs::path& root) {
  std::error_code ec;
  std::vector<fs::path> stale;
  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file(ec) && it->path().extension() == kTempSuffix) stale.push_back(it->path());
  }
  for (const auto& path : stale) remove_quietly(path);
}

}  // namespace

std::expected<void, std::string> recover_pending_commit(const fs::path& root) {
  const fs::path manifest = root / kManifestName;
  std::error_code ec;
  if (fs::exists(manifest, ec)) {
    if (auto entries = read_manifest(manifest)) {
      for (const std::string& entry : *entries) {
        const fs::path target = root / fs::path(entry);
        const fs::path temp = temp_of(target);
        if (!fs::exists(temp, ec)) continue;  // already renamed before the kill
        fs::rename(temp, target, ec);
        if (ec) return std::unexpected(std::format("recovery failed to replace '{}'", target.string()));
      }
      sync_directory(root);
    }
    remove_quietly(manifest);
  }
  remove_stale_temps(root);
  return {};
}

std::expected<void, std::string> commit_files_atomic(const fs::path& root, const std::vector<FileWrite>& files,
                                                     const AtomicSaveOptions& options) {
  int step = 0;
  auto checkpoint = [&] {
    if (step++ == options.crash_at_step) throw Crash{};
  };

  std::vector<std::string> relative;
  std::vector<std::optional<std::string>> previous;
  for (const FileWrite& file : files) {
    fs::path rel = file.path.lexically_relative(root);
    if (rel.empty() || *rel.begin() == "..") {
      return std::unexpected(std::format("'{}' is not under '{}'", file.path.string(), root.string()));
    }
    relative.push_back(rel.generic_string());
    std::error_code ec;
    if (fs::exists(file.path, ec)) {
      auto old_text = read_text_file(file.path);
      if (!old_text) return std::unexpected(old_text.error());
      previous.push_back(std::move(*old_text));
    } else {
      previous.push_back(std::nullopt);
    }
  }
  if (auto pending = recover_pending_commit(root); !pending) return pending;

  const fs::path manifest = root / kManifestName;
  auto discard = [&] {
    for (const FileWrite& file : files) remove_quietly(temp_of(file.path));
    remove_quietly(temp_of(manifest));
    remove_quietly(manifest);
  };

  try {
    for (const FileWrite& file : files) {
      std::error_code ec;
      fs::create_directories(file.path.parent_path(), ec);
      if (auto staged = write_synced(temp_of(file.path), file.text); !staged) {
        discard();
        return staged;
      }
      checkpoint();
    }

    std::string text = std::string(kManifestHeader) + "\n";
    for (const std::string& rel : relative) text += rel + "\n";
    text += std::string(kManifestEnd) + "\n";
    auto written = write_synced(temp_of(manifest), text);
    std::error_code ec;
    if (written) fs::rename(temp_of(manifest), manifest, ec);
    if (!written || ec) {
      discard();
      return std::unexpected(written ? std::format("failed to write '{}'", manifest.string()) : written.error());
    }
    checkpoint();  // committed from here on

    for (std::size_t i = 0; i < files.size(); ++i) {
      std::error_code rename_ec;
      if (static_cast<int>(i) == options.fail_commit_index) {
        rename_ec = std::make_error_code(std::errc::io_error);
      } else {
        fs::rename(temp_of(files[i].path), files[i].path, rename_ec);
      }
      if (rename_ec) {
        discard();
        for (std::size_t j = 0; j < i; ++j) {
          if (previous[j]) {
            const fs::path temp = temp_of(files[j].path);
            if (write_synced(temp, *previous[j])) fs::rename(temp, files[j].path, ec);
            remove_quietly(temp);
          } else {
            remove_quietly(files[j].path);
          }
        }
        const std::string what = static_cast<int>(i) == options.fail_commit_index
                                     ? std::format("simulated failure committing '{}'", files[i].path.string())
                                     : std::format("failed to replace '{}'", files[i].path.string());
        return std::unexpected("rolled back: " + what);
      }
      checkpoint();
    }
    sync_directory(root);
    remove_quietly(manifest);
    checkpoint();
  } catch (const Crash&) {
    return std::unexpected("simulated crash");
  }
  return {};
}

}  // namespace arcane
