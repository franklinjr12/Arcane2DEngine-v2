#include "arcane_editor/audio_wiring.hpp"

#include <functional>

#include "arcane/node.hpp"

namespace arcane::editor {

namespace {

void start_node_sounds(Node* node, AudioSystem& system, const std::vector<AssetEntry>& audio_assets, int& started) {
  if (auto* source = static_cast<AudioSource*>(node->find_component("AudioSource"))) {
    if (source->play_on_start) {
      for (const auto& entry : audio_assets) {
        if (entry.id != source->clip) continue;
        system.play(entry.absolute_path, source->volume, source->loop,
                    source->loop ? AudioBus::Music : AudioBus::Sfx);
        ++started;
        break;
      }
    }
  }
  for (const auto& child : node->children()) {
    start_node_sounds(child.get(), system, audio_assets, started);
  }
}

}  // namespace

int start_play_on_start_sounds(Scene& scene, AudioSystem& system, const std::vector<AssetEntry>& audio_assets) {
  int started = 0;
  for (const auto& root : scene.roots()) {
    start_node_sounds(root.get(), system, audio_assets, started);
  }
  return started;
}

}  // namespace arcane::editor
