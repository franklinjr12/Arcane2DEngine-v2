#pragma once

#include <memory>
#include <span>

#include "arcane/export.hpp"
#include "arcane/renderer.hpp"

namespace arcane {

// Fullscreen post-process chain. begin() redirects drawing into an offscreen color texture of the frame size; the
// normal SpriteRenderer frame then draws into it, and apply() runs each MaterialShader as a fullscreen pass,
// ping-ponging between two textures, the last pass drawing into the framebuffer that was bound at begin().
// In a pass u_texture is the previous pass, v_color is white and v_uv runs 0..1 over the frame (GL convention: v = 0
// at the bottom). GL objects are created on the first begin() and need a current GL context to be destroyed.
class ARCANE_API PostProcessChain {
 public:
  PostProcessChain();
  PostProcessChain(PostProcessChain&& other) noexcept;
  PostProcessChain& operator=(PostProcessChain&& other) noexcept;
  PostProcessChain(const PostProcessChain&) = delete;
  PostProcessChain& operator=(const PostProcessChain&) = delete;
  ~PostProcessChain();

  // Saves the bound framebuffer, sizes the two textures to width x height (reallocated on change) and binds the first.
  // With `letterbox` the final output is confined to that rectangle (scissor) and everything outside it is cleared
  // to black; the passes themselves still run over the whole frame.
  void begin(int width, int height, const DisplayViewport* letterbox = nullptr);
  // Runs `passes` (none: plain copy). Intermediate passes draw with blending off; the last pass draws into the saved
  // framebuffer, cleared to black first, with the material's blend. Afterwards the viewport/scissor are what
  // SpriteRenderer::begin_frame leaves (the letterbox rectangle, or the whole frame) and the sprite blend state is
  // restored, so a HUD can draw straight after. Without a preceding begin() it does nothing.
  void apply(std::span<const MaterialShader* const> passes, float time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace arcane
