#pragma once

#include <expected>
#include <string>

#include "arcane/export.hpp"
#include "arcane/hud.hpp"
#include "arcane/input.hpp"

namespace arcane {

// Top-level project file (analogous to a .sln/.godot file): points the
// editor/player at the scene to load on startup. Foundation B kept this
// deliberately small; Foundation D adds the input action map (doc section
// 8's "input maps and startup scene configuration"), embedded as a nested
// object rather than a separate file since one project has exactly one
// action map today.
struct ProjectFile {
  std::string name;
  std::string main_scene;  // path to a scene JSON file, relative to the project file
  // Path to a prebuilt native game module (.so/.dll), relative to the
  // project file; empty if the project has no custom component types.
  std::string module;
  InputActionMap input_actions;
  // Authored HUD layout (buttons, text, bars), instantiated at Play/startup.
  Hud hud;
};

// Writes canonical project v2 (docs/formats/project.md).
ARCANE_API std::expected<std::string, std::string> save_project_canonical_json(const ProjectFile& project);
// Convenience wrapper; returns an empty string on failure.
ARCANE_API std::string save_project_to_json(const ProjectFile& project);

// Reads project v2 (strict) or legacy format_version 1 (lenient about
// unknown top-level fields, as before).
ARCANE_API std::expected<ProjectFile, std::string> load_project_from_json(const std::string& text);
// Legacy files are checked as strictly as v2 ones; used by migration.
ARCANE_API std::expected<ProjectFile, std::string> load_project_from_json_strict(const std::string& text);

}  // namespace arcane
