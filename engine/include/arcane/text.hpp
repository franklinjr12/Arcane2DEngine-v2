#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "arcane/export.hpp"

namespace arcane {

// UTF-8 text support for the HUD (doc section 7.7). The built-in font is Go
// Regular subset to ASCII + Latin-1 + Latin Extended-A (+ bullet, euro), so
// Portuguese strings render. Everything here is CPU-only and headless.

inline constexpr char32_t kReplacementChar = 0xFFFD;

// Decodes UTF-8 to code points. Malformed input (stray continuation byte,
// truncated sequence, overlong form, surrogate, > U+10FFFF) yields one
// U+FFFD per bad sequence and decoding resumes at the next byte that can
// start a character, so it never reads past the end or throws.
ARCANE_API std::vector<char32_t> decode_utf8(std::string_view text);

// True if the built-in font has a real glyph for `cp`. Other code points are
// drawn as a hollow-box fallback glyph (never silently dropped).
ARCANE_API bool font_has_glyph(char32_t cp) noexcept;

// Size of `text` in pixels when the font's em is `pixel_size` tall. Handles
// '\n' (multiple lines). Missing glyphs advance by the fallback box width.
ARCANE_API void measure_text(std::string_view text, float pixel_size, float& out_w, float& out_h);
ARCANE_API float font_line_height(float pixel_size) noexcept;
// Advance of one code point (fallback box width if the glyph is missing).
ARCANE_API float glyph_advance(char32_t cp, float pixel_size) noexcept;

// Glyph rectangle in an atlas plus its placement, in atlas pixels at
// FontAtlas::pixel_size. x_offset/y_offset position the top-left of the
// bitmap relative to the pen at the baseline (y grows downward).
struct AtlasGlyph {
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  float x_offset = 0, y_offset = 0, width = 0, height = 0, advance = 0;
};

struct FontAtlas {
  int width = 0, height = 0;
  float pixel_size = 0.0f;
  float ascent = 0.0f;  // baseline distance below the line top, at pixel_size
  std::vector<unsigned char> rgba;  // white with coverage in alpha
  std::unordered_map<char32_t, AtlasGlyph> glyphs;
};

// Rasterises every glyph of the built-in font into one RGBA bitmap.
ARCANE_API FontAtlas build_font_atlas();

}  // namespace arcane
