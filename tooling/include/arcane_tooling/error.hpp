#pragma once

#include <string>

#include <json.hpp>

namespace arcane::tooling {

using Json = nlohmann::json;

// Stable symbolic failure reported by every command (docs/cli-reference.md
// error table). Context fields are empty when they do not apply and are
// emitted as JSON null.
struct ToolError {
  std::string code;     // e.g. "PROPERTY_UNKNOWN"
  std::string message;  // human readable
  std::string file;     // project-relative file, if any
  std::string path;     // JSON-pointer-like location inside the file
  std::string hint;     // what to run or change next
};

// Process exit status the `arcane` CLI uses for this error: 2 usage,
// 3 file/project, 4 schema/reference/validation, 5 conflict/ambiguity,
// 6 module, 7 unsupported version.
int exit_code(const ToolError& error);

// {"ok":false,"error":{"code":...,"message":...,"file":...,"path":...,"hint":...}}
Json error_json(const ToolError& error);

}  // namespace arcane::tooling
