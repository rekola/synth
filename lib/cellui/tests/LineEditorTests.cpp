#include "TestFramework.h"

#include <cellui/InputDispatcher.h>
#include <cellui/LineEditor.h>

using namespace cellui;

namespace {
bool press(LineEditor & e, const Input & in) { return in.getKind() != Input::Kind::RELEASE && e.key(InputDispatcher::keyDownFor(in)); }
}  // namespace

namespace {

Input key(int id, bool ctrl = false, bool alt = false) { return Input(id, -1, -1, alt, false, ctrl, false); }

void type(LineEditor & e, const std::string & s) {
  for (char c : s) press(e, Input(c, -1, -1, false, false, false, false));
}

}  // namespace

TEST(line_editor_inserts_at_the_cursor) {
  LineEditor e;
  type(e, "play 120bpm");
  press(e, key(cellui::Key::Left));
  press(e, key(cellui::Key::Left));
  press(e, key(cellui::Key::Left));
  type(e, "X");
  CHECK(e.text() == "play 120Xbpm");
  press(e, key(cellui::Key::Backspace));
  press(e, key(cellui::Key::Backspace));
  CHECK(e.text() == "play 12bpm");
  press(e, key('a', true));
  press(e, key('d', true));
  CHECK(e.text() == "lay 12bpm");
}

TEST(line_editor_words_and_kills) {
  LineEditor e;
  e.setText("one two three");
  press(e, key('b', false, true));
  CHECK(e.cursor() == 8);
  press(e, key('w', true));
  CHECK(e.text() == "one three");
  CHECK(LineEditor::killed() == "two ");
  press(e, key('e', true));
  press(e, key('y', true));
  CHECK(e.text() == "one threetwo ");
  press(e, key('a', true));
  press(e, key('d', false, true));
  CHECK(e.text() == " threetwo ");
  press(e, key('k', true));
  CHECK(e.text().empty());
  e.setText("ab");
  press(e, key('t', true));
  CHECK(e.text() == "ba");
}

TEST(line_editor_keeps_clusters_whole) {
  LineEditor e;
  e.setText("e\xcc\x81x");  // e + combining acute, then x
  press(e, key(cellui::Key::Left));
  press(e, key(cellui::Key::Backspace));
  CHECK(e.text() == "x");
  // Shifted letters arrive in their true case.
  press(e, Input('a', -1, -1, false, true, false, false, Input::Kind::UNKNOWN, 'A'));
  CHECK(e.text() == "Ax");
}

TEST(line_editor_scrolls_to_the_cursor) {
  LineEditor e;
  e.setText("abcdefghij");
  auto v = e.view(5);
  CHECK(v.text == "ghij" && v.cursor_column == 4);
  press(e, key('a', true));
  v = e.view(5);
  CHECK(v.text == "abcde" && v.cursor_column == 0);
  // A wide cluster that does not fit is a space.
  LineEditor w;
  w.setText("ab界");
  press(w, key('a', true));
  CHECK(w.view(3).text == "ab ");
}

TEST(line_editor_leaves_field_keys_to_the_owner) {
  LineEditor e;
  CHECK(!press(e, key(cellui::Key::Enter)));
  CHECK(!press(e, key('g', true)));
  CHECK(!press(e, key(cellui::Key::Up)));
  CHECK(!press(e, key(cellui::Key::Tab)));
}
