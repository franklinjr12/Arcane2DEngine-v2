#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

void register_all_commands(CommandRegistry& registry) {
  register_property_commands(registry);
  register_scene_commands(registry);
  register_component_commands(registry);
  register_prefab_commands(registry);
  register_audio_commands(registry);
  register_atlas_commands(registry);
  register_flow_commands(registry);
  register_debug_commands(registry);
  register_bench_commands(registry);
  register_replay_commands(registry);
  register_tilemap_commands(registry);
  register_hud_commands(registry);
  register_input_commands(registry);
  // These write files as they run, outside persist/dry_run.
  for (const char* name : {"atlas.pack", "atlas.make_clip", "transition.set", "debug.set", "save.config_set", "save.set",
                           "save.remove", "save.migrate", "prefab.save", "replay.save", "replay.run", "tileset.create", "tileset.set_tile", "tileset.set_rule",
                           "tileset.remove_rule", "tilemap.dump_csv"}) {
    registry.mark_immediate_write(name);
  }
}

}  // namespace arcane::tooling
