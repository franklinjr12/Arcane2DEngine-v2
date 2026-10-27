#pragma once

#include <expected>
#include <string>

#include "arcane/export.hpp"
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
};

ARCANE_API std::string save_project_to_json(const ProjectFile& project);
ARCANE_API std::expected<ProjectFile, std::string> load_project_from_json(const std::string& text);

}  // namespace arcane
