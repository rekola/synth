#include "TestFramework.h"

#include <cellui/LineEditor.h>

using namespace cellui;

namespace {

Input key(int id, bool ctrl = false, bool alt = false) { return Input(id, -1, -1, alt, false, ctrl, false); }

void type(LineEditor & e, const std::string & s) {
  for (char c : s) e.offerInput(Input(c, -1, -1, false, false, false, false));
}

}  // namespace

TEST(line_editor_inserts_at_the_cursor) {
  LineEditor e;
  type(e, "play 120bpm");
  e.offerInput(key(NCKEY_LEFT));
  e.offerInput(key(NCKEY_LEFT));
  e.offerInput(key(NCKEY_LEFT));
  type(e, "X");
  CHECK(e.text() == "play 120Xbpm");
  e.offerInput(key(NCKEY_BACKSPACE));
  e.offerInput(key(NCKEY_BACKSPACE));
  CHECK(e.text() == "play 12bpm");
  e.offerInput(key('a', true));
  e.offerInput(key('d', true));
  CHECK(e.text() == "lay 12bpm");
}

TEST(line_editor_words_and_kills) {
  LineEditor e;
  e.setText("one two three");
  e.offerInput(key('b', false, true));
  CHECK(e.cursor() == 8);
  e.offerInput(key('w', true));
  CHECK(e.text() == "one three");
  CHECK(LineEditor::killed() == "two ");
  e.offerInput(key('e', true));
  e.offerInput(key('y', true));
  CHECK(e.text() == "one threetwo ");
  e.offerInput(key('a', true));
  e.offerInput(key('d', false, true));
  CHECK(e.text() == " threetwo ");
  e.offerInput(key('k', true));
  CHECK(e.text().empty());
  e.setText("ab");
  e.offerInput(key('t', true));
  CHECK(e.text() == "ba");
}

TEST(line_editor_keeps_clusters_whole) {
  LineEditor e;
  e.setText("e\xcc\x81x");  // e + combining acute, then x
  e.offerInput(key(NCKEY_LEFT));
  e.offerInput(key(NCKEY_BACKSPACE));
  CHECK(e.text() == "x");
  // Shifted letters arrive in their true case.
  e.offerInput(Input('a', -1, -1, false, true, false, false, Input::Kind::UNKNOWN, 'A'));
  CHECK(e.text() == "Ax");
}

TEST(line_editor_scrolls_to_the_cursor) {
  LineEditor e;
  e.setText("abcdefghij");
  auto v = e.view(5);
  CHECK(v.text == "ghij" && v.cursor_column == 4);
  e.offerInput(key('a', true));
  v = e.view(5);
  CHECK(v.text == "abcde" && v.cursor_column == 0);
  // A wide cluster that does not fit is a space.
  LineEditor w;
  w.setText("ab界");
  w.offerInput(key('a', true));
  CHECK(w.view(3).text == "ab ");
}

TEST(line_editor_leaves_field_keys_to_the_owner) {
  LineEditor e;
  CHECK(!e.offerInput(key(NCKEY_ENTER)));
  CHECK(!e.offerInput(key('g', true)));
  CHECK(!e.offerInput(key(NCKEY_UP)));
  CHECK(!e.offerInput(key(NCKEY_TAB)));
}
