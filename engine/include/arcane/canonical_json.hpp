#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "arcane/export.hpp"

namespace arcane {

// Re-formats `text` as canonical JSON (docs/formats/canonical-json.md):
// two-space indent, sorted keys, one array element per line, binary32
// shortest round-trip floats, LF, trailing newline. Idempotent. Fails on
// malformed JSON, duplicate keys, NaN/infinity, floats out of binary32
// range and invalid UTF-8.
ARCANE_API std::expected<std::string, std::string> canonicalize_json(const std::string& text);

// Prefixes a loader error (`#<pointer>: <message>`) with the file it came from:
// `<path>#<pointer>: <message>`. Errors not starting with '#' become
// `<path>#: <error>`. See docs/formats/errors.md.
ARCANE_API std::string located_error(std::string_view path, const std::string& error);

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

}  // namespace arcane
