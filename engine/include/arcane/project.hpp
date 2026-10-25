#pragma once

#include <expected>
#include <string>

#include "arcane/export.hpp"

namespace arcane {

// Top-level project file (analogous to a .sln/.godot file): points the
// editor/player at the scene to load on startup. Foundation B keeps this
// deliberately small -- asset registries and input maps (doc section 8)
// are out of scope until a later foundation.
struct ProjectFile {
  std::string name;
  std::string main_scene;  // path to a scene JSON file, relative to the project file
};

ARCANE_API std::string save_project_to_json(const ProjectFile& project);
ARCANE_API std::expected<ProjectFile, std::string> load_project_from_json(const std::string& text);

}  // namespace arcane
