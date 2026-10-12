#include "TestFramework.h"

#include <cellui/CellBuffer.h>

using namespace cellui;

TEST(cellbuffer_writes_and_reads_back_plain_text) {
  CellBuffer b(3, 10);
  b.write(0, 0, "hello", {});
  b.write(1, 2, "world", {});
  CHECK(b.toPlainText() == "hello\n  world\n");
}

TEST(cellbuffer_clips_to_the_buffer_and_the_clip_rectangle) {
  CellBuffer b(2, 6);
  b.write(0, 3, "abcdef", {});
  b.write(5, 0, "gone", {});
  b.write(1, 0, "abcdef", {}, 1, 1, 1, 3);
  CHECK(b.toPlainText() == "   abc\n bcd\n");
}

TEST(cellbuffer_wide_characters_take_two_cells) {
  CellBuffer b(1, 6);
  b.write(0, 0, "\xe6\xbc\xa2x", {});
  CHECK(b.at(0, 0).text == "\xe6\xbc\xa2");
  CHECK(b.at(0, 1).continuation);
  CHECK(b.at(0, 2).text == "x");
  CHECK(b.toPlainText() == "\xe6\xbc\xa2x\n");
}

TEST(cellbuffer_wide_character_that_does_not_fit_is_left_out) {
  CellBuffer b(1, 3);
  b.write(0, 2, "\xe6\xbc\xa2", {});
  CHECK(b.at(0, 2).text == " ");
}

TEST(cellbuffer_combining_marks_join_the_cell_before) {
  CellBuffer b(1, 4);
  b.write(0, 0, "e\xcc\x81z", {});
  CHECK(b.at(0, 0).text == "e\xcc\x81");
  CHECK(b.at(0, 1).text == "z");
}

TEST(cellbuffer_ignores_control_characters_and_bad_bytes) {
  CellBuffer b(1, 6);
  b.write(0, 0, "a\tb\n\xff" "c", {});
  CHECK(b.toPlainText() == "ab\xef\xbf\xbd" "c\n");
}

TEST(cellbuffer_clear_resets_style_too) {
  CellBuffer b(1, 4);
  CellBuffer::Style s;
  s.fg = Color("#ff0000");
  s.bold = true;
  b.write(0, 0, "abcd", s);
  b.clear(0, 1, 1, 2);
  CHECK(b.at(0, 0).bold);
  CHECK(!b.at(0, 1).bold && !b.at(0, 1).fg);
  CHECK(b.toPlainText() == "a  d\n");
}

TEST(cellbuffer_ansi_changes_style_only_where_it_changes) {
  CellBuffer b(1, 3);
  CellBuffer::Style red;
  red.fg = Color("#ff0000");
  b.write(0, 0, "ab", red);
  b.write(0, 2, "c", {});
  CHECK(b.toAnsi() == "\x1b[0;38;2;255;0;0mab\x1b[0mc\x1b[0m\n");
}

TEST(cellbuffer_ansi_encodes_each_attribute) {
  CellBuffer b(1, 1);
  CellBuffer::Style s;
  s.bold = s.italic = s.underline = true;
  s.bg = Color("#000102");
  b.write(0, 0, "x", s);
  CHECK(b.toAnsi() == "\x1b[0;1;3;4;48;2;0;1;2mx\x1b[0m\n");
}

TEST(cellbuffer_ansi_limits_rows) {
  CellBuffer b(3, 1);
  b.write(0, 0, "a", {});
  b.write(1, 0, "b", {});
  CHECK(b.toAnsi(1) == "a\x1b[0m\n");
}

TEST(cellbuffer_empty_buffer_has_no_text) {
  CellBuffer b(2, 2);
  CHECK(b.toPlainText().empty());
}
