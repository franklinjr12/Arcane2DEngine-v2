#include "arcane/text.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "font_data.hpp"

namespace arcane {

namespace {

// Code point ranges present in the embedded font subset.
struct Range {
  char32_t first, last;
};
constexpr Range kRanges[] = {{0x20, 0x7E}, {0xA0, 0x17F}, {0x2022, 0x2022}, {0x20AC, 0x20AC}};

// Width of the missing-glyph box, as a fraction of the em.
constexpr float kFallbackAdvance = 0.6f;

struct Font {
  stbtt_fontinfo info{};
  bool ok = false;
  int ascent = 0, descent = 0, line_gap = 0;
  Font() {
    const auto* data = kUiFontData;
    ok = stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0)) != 0;
    if (ok) stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
  }
};

const Font& font() {
  static const Font instance;
  return instance;
}

float em_scale(float pixel_size) {
  const Font& f = font();
  return f.ok ? stbtt_ScaleForMappingEmToPixels(&f.info, pixel_size) : 0.0f;
}

}  // namespace

std::vector<char32_t> decode_utf8(std::string_view text) {
  std::vector<char32_t> out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i]);
    if (lead < 0x80) {
      out.push_back(lead);
      ++i;
      continue;
    }
    int length = 0;
    char32_t cp = 0, minimum = 0;
    if (lead >= 0xC2 && lead <= 0xDF) { length = 2; cp = lead & 0x1F; minimum = 0x80; }
    else if (lead >= 0xE0 && lead <= 0xEF) { length = 3; cp = lead & 0x0F; minimum = 0x800; }
    else if (lead >= 0xF0 && lead <= 0xF4) { length = 4; cp = lead & 0x07; minimum = 0x10000; }
    if (length == 0) {  // continuation byte, 0xC0/0xC1 or 0xF5+
      out.push_back(kReplacementChar);
      ++i;
      continue;
    }
    size_t used = 1;
    while (used < static_cast<size_t>(length) && i + used < text.size() &&
           (static_cast<unsigned char>(text[i + used]) & 0xC0) == 0x80) {
      cp = (cp << 6) | (static_cast<unsigned char>(text[i + used]) & 0x3F);
      ++used;
    }
    const bool complete = used == static_cast<size_t>(length);
    const bool valid = complete && cp >= minimum && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
    out.push_back(valid ? cp : kReplacementChar);
    i += used;  // a truncated sequence consumes only the bytes that belong to it
  }
  return out;
}

bool font_has_glyph(char32_t cp) noexcept {
  const Font& f = font();
  if (!f.ok) return false;
  for (const Range& r : kRanges) {
    if (cp >= r.first && cp <= r.last) return stbtt_FindGlyphIndex(&f.info, static_cast<int>(cp)) != 0;
  }
  return false;
}

float font_line_height(float pixel_size) noexcept {
  const Font& f = font();
  return static_cast<float>(f.ascent - f.descent + f.line_gap) * em_scale(pixel_size);
}

float glyph_advance(char32_t cp, float pixel_size) noexcept {
  if (!font_has_glyph(cp)) return kFallbackAdvance * pixel_size;
  int advance = 0, bearing = 0;
  stbtt_GetCodepointHMetrics(&font().info, static_cast<int>(cp), &advance, &bearing);
  return static_cast<float>(advance) * em_scale(pixel_size);
}

void measure_text(std::string_view text, float pixel_size, float& out_w, float& out_h) {
  float widest = 0.0f, line = 0.0f;
  int lines = 1;
  for (const char32_t cp : decode_utf8(text)) {
    if (cp == U'\n') {
      widest = std::max(widest, line);
      line = 0.0f;
      ++lines;
    } else {
      line += glyph_advance(cp, pixel_size);
    }
  }
  out_w = std::max(widest, line);
  out_h = font_line_height(pixel_size) * static_cast<float>(lines);
}

TextLayout layout_text(std::string_view text, float pixel_size, const TextLayoutOptions& options) {
  const std::vector<char32_t> cps = decode_utf8(text);
  const bool wrap = options.max_width > 0.0f;
  const float pitch = options.line_height > 0.0f ? options.line_height : font_line_height(pixel_size);
  std::vector<float> adv(cps.size());
  for (size_t i = 0; i < cps.size(); ++i) adv[i] = glyph_advance(cps[i], pixel_size);

  struct Line { size_t begin, end; };
  std::vector<Line> lines;
  size_t pos = 0;
  while (true) {
    size_t j = pos, brk = std::string::npos, first_ink = std::string::npos;
    float width = 0.0f;
    size_t end = 0, next = 0;
    bool done = false;
    for (;; ++j) {
      if (j == cps.size() || cps[j] == U'\n') {
        end = j;
        next = j + 1;
        done = j == cps.size();
        break;
      }
      if (wrap && j > pos && width + adv[j] > options.max_width) {
        if (cps[j] == U' ') {
          end = j;
        } else if (brk != std::string::npos) {
          end = brk;
        } else {
          end = j;
        }
        next = end;
        while (next < cps.size() && cps[next] == U' ') ++next;
        break;
      }
      width += adv[j];
      if (cps[j] != U' ') {
        if (first_ink == std::string::npos) first_ink = j;
      } else if (first_ink != std::string::npos) {
        brk = j;
      }
    }
    lines.push_back({pos, end});
    if (done) break;
    pos = next;
  }

  TextLayout out;
  float widest = 0.0f;
  for (const Line& l : lines) {
    size_t e = l.end;
    if (wrap) while (e > l.begin && cps[e - 1] == U' ') --e;  // trailing spaces never count toward alignment
    float w = 0.0f;
    for (size_t i = l.begin; i < e; ++i) w += adv[i];
    out.line_widths.push_back(w);
    widest = std::max(widest, w);
  }
  out.width = wrap ? options.max_width : widest;
  out.height = pitch * static_cast<float>(lines.size());
  for (size_t li = 0; li < lines.size(); ++li) {
    const float slack = out.width - out.line_widths[li];
    float x = options.align == TextAlign::Center ? slack * 0.5f : options.align == TextAlign::Right ? slack : 0.0f;
    if (x < 0.0f) x = 0.0f;  // an unbreakable glyph wider than max_width starts at the left edge
    for (size_t i = lines[li].begin; i < lines[li].end; ++i) {
      if (cps[i] != U' ') {
        out.glyphs.push_back({cps[i], x, pitch * static_cast<float>(li), adv[i], !font_has_glyph(cps[i]), static_cast<int>(li)});
      }
      x += adv[i];
    }
  }
  return out;
}

FontAtlas build_font_atlas() {
  constexpr float kAtlasPixelSize = 40.0f;
  FontAtlas atlas;
  atlas.width = 1024;
  atlas.height = 512;
  atlas.pixel_size = kAtlasPixelSize;
  const Font& f = font();
  if (!f.ok) return atlas;
  atlas.ascent = static_cast<float>(f.ascent) * em_scale(kAtlasPixelSize);

  std::vector<unsigned char> coverage(static_cast<size_t>(atlas.width) * atlas.height, 0);
  stbtt_pack_context pack;
  if (!stbtt_PackBegin(&pack, coverage.data(), atlas.width, atlas.height, 0, 1, nullptr)) return atlas;

  // Pack the ranges, remembering which code points each packed range holds.
  std::vector<stbtt_pack_range> ranges;
  std::vector<std::vector<stbtt_packedchar>> chars;
  chars.reserve(std::size(kRanges));
  for (const Range& r : kRanges) {
    chars.emplace_back(r.last - r.first + 1);
    stbtt_pack_range range{};
    range.font_size = kAtlasPixelSize;
    range.first_unicode_codepoint_in_range = static_cast<int>(r.first);
    range.num_chars = static_cast<int>(r.last - r.first + 1);
    range.chardata_for_range = chars.back().data();
    ranges.push_back(range);
  }
  stbtt_PackFontRanges(&pack, kUiFontData, 0, ranges.data(), static_cast<int>(ranges.size()));
  stbtt_PackEnd(&pack);

  for (size_t ri = 0; ri < ranges.size(); ++ri) {
    for (size_t k = 0; k < chars[ri].size(); ++k) {
      const char32_t cp = kRanges[ri].first + static_cast<char32_t>(k);
      if (!font_has_glyph(cp)) continue;
      const stbtt_packedchar& c = chars[ri][k];
      AtlasGlyph g;
      g.u0 = static_cast<float>(c.x0) / static_cast<float>(atlas.width);
      g.v0 = static_cast<float>(c.y0) / static_cast<float>(atlas.height);
      g.u1 = static_cast<float>(c.x1) / static_cast<float>(atlas.width);
      g.v1 = static_cast<float>(c.y1) / static_cast<float>(atlas.height);
      g.x_offset = c.xoff;
      g.y_offset = c.yoff;
      g.width = static_cast<float>(c.x1 - c.x0);
      g.height = static_cast<float>(c.y1 - c.y0);
      g.advance = c.xadvance;
      atlas.glyphs.emplace(cp, g);
    }
  }

  atlas.rgba.resize(coverage.size() * 4);
  for (size_t p = 0; p < coverage.size(); ++p) {
    atlas.rgba[p * 4 + 0] = atlas.rgba[p * 4 + 1] = atlas.rgba[p * 4 + 2] = 255;
    atlas.rgba[p * 4 + 3] = coverage[p];
  }
  return atlas;
}

}  // namespace arcane
