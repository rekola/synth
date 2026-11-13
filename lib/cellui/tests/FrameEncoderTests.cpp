#include "TestFramework.h"

#include <cellui/term/FrameEncoder.h>

#include <string>

using namespace cellui;
using namespace cellui::term;

namespace {

// Escaped like the specification's captures: ␛ for ESC, ^X for controls.
std::string show(const std::string & bytes) {
  std::string out;
  for (char ch : bytes) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c == 0x1b) out += "␛";
    else if (c < 0x20) out += std::string("^") + static_cast<char>(c ^ 0x40);
    else out += ch;
  }
  return out;
}

bool same(const std::string & got, const std::string & want) {
  if (got == want) return true;
  std::fprintf(stderr, "    got:  %s\n    want: %s\n", show(got).c_str(), show(want).c_str());
  return false;
}

EncoderProfile profile(ColorDepth colors, SyncForm sync = SyncForm::NONE, bool column_move = true) {
  EncoderProfile p;
  p.colors = colors;
  p.sync = sync;
  p.column_move = column_move;
  return p;
}

const std::string kClear = "\x1b[0m\x1b[H\x1b[2J";

CellBuffer::Style fg(int r, int g, int b) {
  CellBuffer::Style s;
  s.fg = Color(r, g, b);
  return s;
}

}  // namespace

TEST(encoder_quantizes_to_the_real_256_colour_levels) {
  CHECK(nearest256(Color(0x5f, 0x87, 0xaf)) == 67);
  CHECK(nearest256(Color(0, 0, 0)) == 16);
  CHECK(nearest256(Color(255, 255, 255)) == 231);
  CHECK(nearest256(Color(0x80, 0x80, 0x80)) == 244);
  CHECK(nearest256(Color(0x08, 0x08, 0x08)) == 232);
  CHECK(nearest256(Color(255, 0, 0)) == 196);
  CHECK(nearest256(Color(0xee, 0xee, 0xee)) == 255);
  CHECK(nearest8(Color(0x80, 0x80, 0x80)) == 7);
  CHECK(nearest8(Color(0x5f, 0x87, 0xaf)) == 6);
  CHECK(nearest8(Color(0x12, 0x34, 0x56)) == 0);
}

TEST(encoder_first_frame_clears_then_draws_only_what_is_not_blank) {
  FrameEncoder e(profile(ColorDepth::C256));
  CellBuffer f(5, 20);
  f.write(1, 2, "Hi", fg(255, 0, 0));
  CHECK(same(e.encode(f), kClear + "\x1b[2;3H\x1b[38;5;196mHi"));
}

TEST(encoder_unchanged_frame_is_no_bytes) {
  FrameEncoder e(profile(ColorDepth::C256, SyncForm::MODE));
  CellBuffer f(5, 20);
  f.write(0, 0, "abc", {});
  e.encode(f);
  CHECK(e.encode(f).empty());
}

TEST(encoder_one_changed_cell) {
  FrameEncoder e(profile(ColorDepth::C256));
  CellBuffer f(7, 24);
  e.encode(f);
  f.write(2, 4, "J", fg(255, 204, 0));
  CHECK(same(e.encode(f), "\x1b[3;5H\x1b[38;5;220mJ"));
  FrameEncoder t(profile(ColorDepth::TRUECOLOR, SyncForm::MODE));
  CellBuffer g(7, 24);
  t.encode(g);
  g.write(2, 4, "J", fg(255, 204, 0));
  CHECK(same(t.encode(g), "\x1b[?2026h\x1b[3;5H\x1b[38;2;255;204;0mJ\x1b[?2026l"));
}

TEST(encoder_runs_need_no_moves_and_gaps_use_column_moves) {
  FrameEncoder e(profile(ColorDepth::C256));
  CellBuffer f(3, 20);
  e.encode(f);
  f.write(0, 2, "ab", {});
  f.write(0, 10, "c", {});
  f.write(2, 0, "d", {});
  CHECK(same(e.encode(f), "\x1b[3Gab\x1b[11Gc\x1b[3;1Hd"));
  // Without a column move every jump is a full position, but a run still
  // needs none.
  FrameEncoder vt(profile(ColorDepth::NONE, SyncForm::NONE, false));
  CellBuffer g(3, 20);
  vt.encode(g);
  g.write(0, 2, "ab", fg(255, 0, 0));
  g.write(0, 10, "c", {});
  CHECK(same(vt.encode(g), "\x1b[1;3Hab\x1b[1;11Hc"));
}

TEST(encoder_sends_only_style_and_colour_changes) {
  FrameEncoder e(profile(ColorDepth::TRUECOLOR));
  CellBuffer f(2, 20);
  e.encode(f);
  CellBuffer::Style bold = fg(1, 2, 3);
  bold.weight = 700;
  CellBuffer::Style plain = fg(1, 2, 3);
  plain.bg = Color(4, 5, 6);
  f.write(0, 0, "ab", bold);
  f.write(0, 2, "cd", plain);
  f.write(0, 4, "e", {});
  CHECK(same(e.encode(f), "\x1b[1;38;2;1;2;3mab\x1b[22;48;2;4;5;6mcd\x1b[39;49me"));
}

TEST(encoder_eight_colours) {
  FrameEncoder e(profile(ColorDepth::C8));
  CellBuffer f(1, 10);
  e.encode(f);
  CellBuffer::Style s = fg(255, 0, 0);
  s.bg = Color(0, 0, 255);
  f.write(0, 0, "xy", s);
  CHECK(same(e.encode(f), "\x1b[31;44mxy"));
}

TEST(encoder_kitty_default_background_is_nudged) {
  EncoderProfile p = profile(ColorDepth::TRUECOLOR);
  p.distinct_from = Color(0, 0, 0);
  FrameEncoder e(p);
  CellBuffer f(1, 5);
  e.encode(f);
  CellBuffer::Style s;
  s.bg = Color(0, 0, 0);
  f.write(0, 0, " ", s);
  CHECK(same(e.encode(f), "\x1b[48;2;0;0;1m "));
}

TEST(encoder_last_cell_is_followed_by_a_move) {
  FrameEncoder e(profile(ColorDepth::C256));
  CellBuffer f(24, 80);
  e.encode(f);
  f.write(23, 79, "Z", {});
  f.write(0, 0, "a", {});
  CHECK(same(e.encode(f), "a\x1b[24;80HZ"));
  // Writing the right edge leaves the column unknown.
  CellBuffer g(2, 4);
  FrameEncoder e2(profile(ColorDepth::C256));
  e2.encode(g);
  g.write(0, 3, "x", {});
  g.write(1, 0, "y", {});
  CHECK(same(e2.encode(g), "\x1b[4Gx\x1b[2;1Hy"));
}

TEST(encoder_wide_and_risky_clusters) {
  FrameEncoder e(profile(ColorDepth::C256));
  CellBuffer f(1, 10);
  e.encode(f);
  f.write(0, 0, "界a", {});
  CHECK(same(e.encode(f), "界a"));
  CellBuffer g(1, 10);
  FrameEncoder e2(profile(ColorDepth::C256));
  e2.encode(g);
  g.write(0, 0, "⏹b", {});
  CHECK(same(e2.encode(g), "⏹\x1b[2Gb"));
  // A wide glyph replaced by a narrow one: the freed right half is drawn.
  f.write(0, 0, "xy", {});
  CHECK(same(e.encode(f), "\x1b[1Gxy"));
}

TEST(encoder_size_change_redraws_everything) {
  FrameEncoder e(profile(ColorDepth::C256, SyncForm::MODE));
  CellBuffer f(2, 4);
  f.write(0, 0, "ab", {});
  e.encode(f);
  CellBuffer g(3, 5);
  g.write(0, 0, "ab", {});
  CHECK(same(e.encode(g), "\x1b[?2026h" + kClear + "ab\x1b[?2026l"));
  e.invalidate();
  CHECK(same(e.encode(g), "\x1b[?2026h" + kClear + "ab\x1b[?2026l"));
}

TEST(encoder_visible_cursor_is_placed_after_the_frame) {
  FrameEncoder e(profile(ColorDepth::C256));
  CellBuffer f(24, 80);
  e.encode(f);
  CHECK(same(e.encode(f, {true, 23, 10}), "\x1b[24;11H\x1b[?12l\x1b[?25h"));
  CHECK(e.encode(f, {true, 23, 10}).empty());
  f.write(0, 0, "a", {});
  CHECK(same(e.encode(f, {true, 23, 10}), "\x1b[?25l\x1b[1;1Ha\x1b[24;11H\x1b[?12l\x1b[?25h"));
  CHECK(same(e.encode(f, {true, 23, 12}), "\x1b[13G"));
  CHECK(same(e.encode(f), "\x1b[?25l"));
}

TEST(encoder_sends_a_glyph_in_its_own_background_as_a_space) {
  FrameEncoder e(profile(ColorDepth::TRUECOLOR));
  CellBuffer f(1, 4);
  e.encode(f);
  CellBuffer::Style same;
  same.fg = Color(9, 9, 9);
  same.bg = Color(9, 9, 9);
  f.write(0, 0, "▌", same);
  std::string out = e.encode(f);
  CHECK(out.find("▌") == std::string::npos && out.back() == ' ');
  same.underline = true;
  f.write(0, 1, "▌", same);
  CHECK(e.encode(f).find("▌") != std::string::npos);
}

TEST(encoder_sends_a_wide_colour_as_its_srgb_clip_and_only_when_the_clip_changes) {
  FrameEncoder e(profile(ColorDepth::TRUECOLOR));
  e.setClock([] { return 0.0; });
  CellBuffer f(2, 20);
  e.encode(f);
  CellBuffer::Style lit;
  lit.fg = Color(200, 30, 30) * 6.0f;
  auto clip = lit.fg->toSrgb8();
  f.write(0, 0, "J", lit);
  // Lit this far it is bold too, the terminal's way of brighter.
  CHECK(same(e.encode(f), "\x1b[1;38;2;" + std::to_string(int{clip[0]}) + ";" + std::to_string(int{clip[1]}) + ";" + std::to_string(int{clip[2]}) + "mJ"));
  // Lit far enough it is plain white, and lit further it is the same: nothing to send.
  lit.fg = Color(200, 30, 30) * 6000.0f;
  f.write(0, 0, "J", lit);
  e.encode(f);
  lit.fg = Color(200, 30, 30) * 9000.0f;
  CHECK(lit.fg->toSrgb8() == (Color(200, 30, 30) * 6000.0f).toSrgb8());
  f.write(0, 0, "J", lit);
  CHECK(e.encode(f).empty());
}

TEST(encoder_writes_a_foreground_lit_past_white_bold_and_plain_again_when_it_fades) {
  FrameEncoder e(profile(ColorDepth::TRUECOLOR));
  e.setClock([] { return 0.0; });
  CellBuffer f(2, 20);
  e.encode(f);
  CellBuffer::Style style;
  style.fg = Color::linear(0.5f, 0.5f, 0.5f) * 3.0f;  // 1.5: lit past white
  f.write(0, 0, "J", style);
  CHECK(e.encode(f).find("\x1b[1;") != std::string::npos);
  // Lit but under the bold threshold, and in the same colour on the display: plain again.
  style.fg = Color::linear(0.5f, 0.5f, 0.5f) * 1.2f;
  f.write(0, 0, "J", style);
  CHECK(e.encode(f).find("22") != std::string::npos);
  // A colour at white or less is never bold, and one the app asked bold for still is.
  style.fg = Color::linear(0.9f, 0.9f, 0.9f);
  f.write(0, 1, "K", style);
  CHECK(e.encode(f).find("\x1b[1") == std::string::npos);
  style.weight = 700;
  f.write(0, 2, "L", style);
  CHECK(e.encode(f).find("\x1b[1m") != std::string::npos);
}

TEST(encoder_writes_a_lit_cell_bold_for_its_first_moments_only) {
  double now = 0.0;
  FrameEncoder e(profile(ColorDepth::TRUECOLOR));
  e.setClock([&] { return now; });
  CellBuffer f(2, 20);
  e.encode(f);
  CellBuffer::Style lit;
  lit.fg = Color::linear(1.3f, 1.3f, 1.3f);  // only just past white: no velocity or tint changes that
  now = 1.0;
  f.write(0, 0, "J", lit);
  CHECK(e.encode(f).find("\x1b[1;") != std::string::npos);
  // Within the hold nothing changes, so nothing is sent.
  now = 1.0 + FrameEncoder::kBoldHold * 0.5;
  CHECK(e.encode(f).empty());
  // Past it the cell is plain again, though its colour is still lit.
  now = 1.0 + FrameEncoder::kBoldHold * 1.5;
  CHECK(e.encode(f).find("22") != std::string::npos);
  now += 1.0;
  CHECK(e.encode(f).empty());
  // It falls to a normal colour, and is lit again: bold again, for another hold.
  CellBuffer::Style plain;
  plain.fg = Color::linear(0.8f, 0.8f, 0.8f);
  now += 0.1;
  f.write(0, 0, "J", plain);
  e.encode(f);
  now += 0.1;
  f.write(0, 0, "J", lit);
  CHECK(e.encode(f).find("\x1b[1;") != std::string::npos);
}

namespace {

CellBuffer::Style colours(const Color & fg, const Color & bg) {
  CellBuffer::Style s;
  s.fg = fg;
  s.bg = bg;
  return s;
}

}  // namespace

TEST(encoder_16_colours_use_the_bright_codes_and_the_palettes_own_values) {
  // The standard palette's own values come back as their own index.
  for (int i = 0; i < 16; i++) CHECK(nearest16(ansi16Color(i)) == i);
  FrameEncoder e(profile(ColorDepth::C16));
  CellBuffer f(3, 20);
  f.write(0, 0, "a", colours(ansi16Color(1), ansi16Color(4)));    // red on blue: 31, 44
  f.write(0, 1, "b", colours(ansi16Color(9), ansi16Color(12)));   // bright red on bright blue: 91, 104
  CHECK(same(e.encode(f), kClear + "\x1b[31;44ma\x1b[91;104mb"));
}

TEST(encoder_16_and_8_colours_leave_the_terminals_own_defaults_to_the_terminal) {
  for (ColorDepth depth : {ColorDepth::C16, ColorDepth::C8}) {
    EncoderProfile p = profile(depth);
    p.default_fg = Color("#d0d0d0");
    p.default_bg = Color("#101010");
    FrameEncoder e(p);
    CellBuffer f(2, 10);
    // Text and background in exactly the terminal's colours: no colour code at all.
    f.write(0, 0, "x", colours(Color("#d0d0d0"), Color("#101010")));
    f.write(0, 1, "y", colours(ansi16Color(2), Color("#101010")));
    CHECK(same(e.encode(f), kClear + "x\x1b[32my"));
  }
}

TEST(encoder_without_colour_marks_a_highlight_by_reverse_video_and_a_grey_text_by_dim) {
  EncoderProfile p = profile(ColorDepth::NONE);
  p.default_fg = Color("#f0f0f0");
  p.default_bg = Color("#101010");
  FrameEncoder e(p);
  CellBuffer f(2, 20);
  f.write(0, 0, "a", colours(Color("#f0f0f0"), Color("#101010")));  // the terminal's own: plain
  f.write(0, 1, "b", colours(Color("#050505"), Color("#75d4eb")));  // a bright bar: reverse
  f.write(0, 2, "c", colours(Color("#7a7a7a"), Color("#1c1c1c")));  // a quiet grey: dim
  f.write(0, 3, "d", colours(Color("#f0f0f0"), Color("#2c2c2c")));  // a raised surface is no highlight
  CHECK(same(e.encode(f), kClear + "a\x1b[7mb\x1b[2;27mc\x1b[22md"));
  // A highlight over a bold cell keeps the bold; ending a bold that dim shares keeps the dim.
  CellBuffer g(2, 20);
  CellBuffer::Style bold;
  bold.weight = 700;
  bold.fg = Color("#7a7a7a");
  bold.bg = Color("#1c1c1c");
  g.write(0, 0, "e", bold);
  g.write(0, 1, "f", colours(Color("#7a7a7a"), Color("#1c1c1c")));
  FrameEncoder h(p);
  CHECK(same(h.encode(g), kClear + "\x1b[1;2me\x1b[22;2mf"));
}
