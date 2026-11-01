#pragma once

#include "arcane/canonical_json.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace arcane {

struct AtomicSaveOptions {
  int fail_commit_index = -1;  // test hook: ordinary failure committing the Nth file (rolls back)
  int crash_at_step = -1;      // test hook: stop dead after the Nth step, leaving disk as a kill would
};

// Number of crash points of a commit of `file_count` files: one per staged file, the manifest,
// each rename, and removing the manifest.
constexpr int atomic_save_step_count(std::size_t file_count) { return static_cast<int>(2 * file_count + 2); }

// Crash-atomic multi-file replace. Every file is staged (written and fsynced) as NAME.arcane-tmp, then a
// manifest ROOT/.arcane-commit naming the files is written and renamed into place: that rename is the
// commit point. The staged files are then renamed over their targets and the manifest is removed.
// A kill before the commit point leaves the old set, a kill after it is rolled forward by
// recover_pending_commit(), so a reader sees entirely old or entirely new files. An ordinary
// failure during the renames restores the previous bytes; the error starts with "rolled back:".
// All paths must be under `root`. Linux and Windows (mingw/MSVC) paths; not safe against a second
// process saving into the same root concurrently.
ARCANE_API std::expected<void, std::string> commit_files_atomic(const std::filesystem::path& root,
                                                                const std::vector<FileWrite>& files,
                                                                const AtomicSaveOptions& options = {});

// Call when opening a project: finishes an interrupted commit (manifest present) or discards stale
// .arcane-tmp files and an unfinished manifest (before the commit point). Returns an error only
// if roll-forward fails; the manifest is then kept so a later open can retry.
ARCANE_API std::expected<void, std::string> recover_pending_commit(const std::filesystem::path& root);

}  // namespace arcane
