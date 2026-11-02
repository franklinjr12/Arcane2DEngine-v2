#include <cmath>
#include <string>

#include "arcane/hud.hpp"
#include "arcane/text.hpp"
#include "test_assert.hpp"

namespace {

using arcane::decode_utf8;
using arcane::kReplacementChar;

void test_decode_valid() {
  const auto cps = decode_utf8("a\xC3\xA7\xC3\xA3o \xE2\x82\xAC \xF0\x9F\x98\x80");  // aço-ish, euro, emoji
  const std::u32string expected = U"a\u00E7\u00E3o \u20AC \U0001F600";  // escaped: MSVC reads sources in the system codepage
  ARCANE_ASSERT_TRUE(std::u32string(cps.begin(), cps.end()) == expected);
  ARCANE_ASSERT_TRUE(decode_utf8("").empty());
}

void test_decode_invalid() {
  auto one = [](const char* bytes) { return decode_utf8(bytes); };
  ARCANE_ASSERT_TRUE(one("\x80").size() == 1 && one("\x80")[0] == kReplacementChar);   // stray continuation
  ARCANE_ASSERT_TRUE(one("\xC3").size() == 1 && one("\xC3")[0] == kReplacementChar);   // truncated at end
  ARCANE_ASSERT_TRUE(one("\xE2\x82").size() == 1 && one("\xE2\x82")[0] == kReplacementChar);
  const auto mid = one("\xC3z");  // truncated, next byte is ASCII and must survive
  ARCANE_ASSERT_TRUE(mid.size() == 2 && mid[0] == kReplacementChar && mid[1] == U'z');
  ARCANE_ASSERT_TRUE(one("\xC0\x80")[0] == kReplacementChar);           // overlong NUL
  ARCANE_ASSERT_TRUE(one("\xE0\x80\x80")[0] == kReplacementChar);       // overlong
  ARCANE_ASSERT_TRUE(one("\xED\xA0\x80")[0] == kReplacementChar);       // surrogate
  ARCANE_ASSERT_TRUE(one("\xF4\x90\x80\x80")[0] == kReplacementChar);   // > U+10FFFF
  ARCANE_ASSERT_TRUE(one("\xFF").size() == 1 && one("\xFF")[0] == kReplacementChar);
}

void test_font_coverage() {
  for (const char32_t cp : std::u32string(U"AZaz09 \u00C0\u00E7\u00E3\u00F5\u00FC\u00A9\u0141\u017E")) {
    ARCANE_ASSERT_TRUE(arcane::font_has_glyph(cp));
  }
  ARCANE_ASSERT_TRUE(!arcane::font_has_glyph(0x4E2D));    // CJK: not in the subset
  ARCANE_ASSERT_TRUE(!arcane::font_has_glyph(0x1F600));
  ARCANE_ASSERT_TRUE(!arcane::font_has_glyph(kReplacementChar));
}

void test_measurement() {
  float w = 0, h = 0, w2 = 0, h2 = 0;
  arcane::measure_text("Ola", 16.0f, w, h);
  ARCANE_ASSERT_TRUE(w > 0 && h > 0);
  arcane::measure_text("Ola", 32.0f, w2, h2);
  ARCANE_ASSERT_TRUE(std::abs(w2 - 2 * w) < 0.01f && std::abs(h2 - 2 * h) < 0.01f);
  // Accented letters decode to one glyph each: same width class as the base letter.
  float wa = 0, ha = 0, wb = 0, hb = 0;
  arcane::measure_text("a", 20.0f, wa, ha);
  arcane::measure_text("\xC3\xA3", 20.0f, wb, hb);  // ã
  ARCANE_ASSERT_TRUE(std::abs(wa - wb) < 0.01f);
  // Two lines are taller, and width is the widest line.
  float wl = 0, hl = 0;
  arcane::measure_text("Ola\nOla", 16.0f, wl, hl);
  ARCANE_ASSERT_TRUE(std::abs(wl - w) < 0.01f && std::abs(hl - 2 * h) < 0.01f);
}

void test_fallback_glyph() {
  // Missing glyph advances by the fallback box, invalid bytes count as one fallback each.
  float w = 0, h = 0, wf = 0, hf = 0;
  arcane::measure_text("\xE4\xB8\xAD", 10.0f, w, h);  // U+4E2D
  ARCANE_ASSERT_TRUE(w > 0);
  arcane::measure_text("\xFF", 10.0f, wf, hf);
  ARCANE_ASSERT_TRUE(std::abs(w - wf) < 0.01f);
  ARCANE_ASSERT_TRUE(std::abs(arcane::glyph_advance(0x4E2D, 10.0f) - w) < 0.01f);
}

void test_hud_text_size_uses_utf8() {
  float w1 = 0, h1 = 0, w2 = 0, h2 = 0;
  arcane::hud_text_size("A\xC3\x87\xC3\x83O", 1.0f, w1, h1);  // AÇÃO
  arcane::hud_text_size("AREO", 1.0f, w2, h2);
  ARCANE_ASSERT_TRUE(w1 > 0 && h1 == h2);
}

void test_atlas() {
  const arcane::FontAtlas atlas = arcane::build_font_atlas();
  ARCANE_ASSERT_TRUE(!atlas.rgba.empty() && atlas.glyphs.size() > 300);
  ARCANE_ASSERT_TRUE(atlas.glyphs.contains(U'\u00E7') && atlas.glyphs.contains(U'A'));
  ARCANE_ASSERT_TRUE(!atlas.glyphs.contains(0x4E2D));
  const auto& a = atlas.glyphs.at(U'A');
  ARCANE_ASSERT_TRUE(a.width > 0 && a.height > 0 && a.u1 > a.u0 && a.v1 > a.v0);
  // Every glyph's inked area must contain some coverage (not an empty atlas).
  size_t inked = 0;
  for (size_t i = 3; i < atlas.rgba.size(); i += 4) inked += atlas.rgba[i] > 128;
  ARCANE_ASSERT_TRUE(inked > 1000);
}

}  // namespace

int main() {
  test_decode_valid();
  test_decode_invalid();
  test_font_coverage();
  test_measurement();
  test_fallback_glyph();
  test_hud_text_size_uses_utf8();
  test_atlas();
  return 0;
}
