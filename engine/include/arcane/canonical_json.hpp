#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "arcane/export.hpp"

namespace arcane {

// Re-formats `text` as canonical JSON (docs/formats/canonical-json.md):
// two-space indent, sorted keys, one array element per line, binary32
// shortest round-trip floats, LF, trailing newline. Idempotent. Fails on
// malformed JSON, duplicate keys, NaN/infinity, floats out of binary32
// range and invalid UTF-8.
ARCANE_API std::expected<std::string, std::string> canonicalize_json(const std::string& text);

ARCANE_API std::expected<std::string, std::string> read_text_file(const std::filesystem::path& path);

// Writes `text` to `<path>.arcane-tmp`, closes it, then renames it over
// `path`: readers never see a half-written file and a failed write leaves
// the old file untouched.
ARCANE_API std::expected<void, std::string> write_text_file_atomic(const std::filesystem::path& path,
                                                                   const std::string& text);

// The two halves of write_text_file_atomic, for committing several files as
// one unit (migration): stage everything first, then commit each file.
ARCANE_API std::expected<void, std::string> stage_text_file(const std::filesystem::path& path,
                                                            const std::string& text);
ARCANE_API std::expected<void, std::string> commit_staged_file(const std::filesystem::path& path);
ARCANE_API std::filesystem::path staging_path(const std::filesystem::path& path);

struct FileWrite {
  std::filesystem::path path;
  std::string text;
};
struct FileTransactionOptions {
  int fail_commit_index = -1;  // test hook: simulate a failure committing the Nth file
};

// Replaces several files as one unit for ordinary failures: every file is staged first, then
// committed in order. If a commit fails, files already committed are restored to their previous
// bytes (or removed if they did not exist) and the error starts with "rolled back:". Not
// crash-atomic: a kill between two renames can leave a mix. Leaves no .arcane-tmp behind.
ARCANE_API std::expected<void, std::string> write_text_files_transactional(
    const std::vector<FileWrite>& files, const FileTransactionOptions& options = {});

}  // namespace arcane
