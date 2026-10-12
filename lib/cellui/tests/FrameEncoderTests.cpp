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
  bold.bold = true;
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
