#include "arcane/audio.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

void register_audio_commands(CommandRegistry& registry) {
  registry.add(
      {"audio.backend",
       "Report which audio backend a visible session would use. By default no device is opened and the result is the "
       "null backend; with probe_device=true a device is opened briefly and the fallback reason is reported.",
       {{"type", "object"},
        {"properties", {{"probe_device", {{"type", "boolean"}}}}},
        {"additionalProperties", false}},
       {{"type", "object"}, {"properties", {{"backend", {{"type", "string"}}}, {"probed", {{"type", "boolean"}}}}}},
       false},
      [](ProjectContext&, const Json& args) -> std::expected<CommandResult, ToolError> {
        const bool probe = args.value("probe_device", false);
        std::string diagnostic;
        auto system = AudioSystem::create_preferred(!probe, diagnostic);
        if (!system) {
          return std::unexpected(ToolError{"AUDIO_UNAVAILABLE", system.error(), "", "", "Playback falls back to silence"});
        }
        Json diag = diagnostic.empty() ? Json(nullptr) : Json(diagnostic);
        return CommandResult{{{"backend", system->is_null_backend() ? "null" : "miniaudio"},
                              {"probed", probe},
                              {"diagnostic", diag}},
                             std::nullopt};
      });
}

}  // namespace arcane::tooling
