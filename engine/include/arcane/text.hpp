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

// Text layout: word wrap, alignment and line height (CPU-only, deterministic).
// Positions are the top-left of each glyph cell (pen x, line top y) in pixels
// for an em of `pixel_size`; add the atlas offsets to draw. Invalid UTF-8 and
// missing glyphs become the fallback glyph, so every input yields a layout.
enum class TextAlign { Left, Center, Right };

struct TextLayoutOptions {
  float max_width = 0.0f;    // wrap width in pixels; <= 0 disables wrapping
  TextAlign align = TextAlign::Left;  // relative to max_width (or the widest line when not wrapping)
  float line_height = 0.0f;  // line pitch in pixels; <= 0 uses font_line_height()
};

struct LaidOutGlyph {
  char32_t cp = 0;     // code point as drawn (U+FFFD for invalid input)
  float x = 0, y = 0;
  float advance = 0;
  bool fallback = false;  // true when the font lacks a glyph and the box is drawn
  int line = 0;
};

struct TextLayout {
  std::vector<LaidOutGlyph> glyphs;  // spaces and newlines take room but are not listed
  std::vector<float> line_widths;    // ink-advance width of each line (trailing spaces trimmed on wrap)
  float width = 0, height = 0;       // width = max_width when wrapping, else widest line
};

// Wrap breaks at spaces; a word wider than max_width is split between
// characters (every line holds at least one character, so it always ends).
// Spaces at a soft break are dropped; '\n' forces a break.
ARCANE_API TextLayout layout_text(std::string_view text, float pixel_size, const TextLayoutOptions& options = {});

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
