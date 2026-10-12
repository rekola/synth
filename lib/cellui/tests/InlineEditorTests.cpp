#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/InlineEditor.h>

using namespace cellui;

namespace {
Input key(int id, bool ctrl = false) { return Input(id, 0, 0, false, false, ctrl, false, Input::Kind::PRESS); }
}  // namespace

TEST(inlineeditor_opens_paints_its_backdrop_and_asks_for_a_redraw) {
  StyleProvider styles;
  CellBuffer b(2, 12);
  BufferPlane root(b, styles);
  InlineEditor editor(root);
  CHECK(!editor.isOpen());
  CHECK(!editor.offerInput(key('a')));
  InlineEditor::Field field;
  field.row = 1;
  field.col = 3;
  field.width = 5;
  field.backdrop = Color("#102030");
  CHECK(editor.open(field, [](std::string) { }));
  CHECK(editor.isOpen());
  root.refresh();
  CHECK(b.at(1, 3).bg && b.at(1, 3).bg->getBlue() == 0x30);
  CHECK(b.at(1, 8).text == " " && !b.at(1, 8).bg);  // just past the field
  CHECK(editor.consumeRedrawRequest());
  CHECK(!editor.consumeRedrawRequest());
}

TEST(inlineeditor_enter_commits_and_ctrl_g_cancels) {
  StyleProvider styles;
  CellBuffer b(1, 12);
  BufferPlane root(b, styles);
  InlineEditor editor(root);
  int commits = 0;
  editor.open({}, [&](std::string) { commits++; });
  CHECK(editor.offerInput(key(NCKEY_ENTER)));
  CHECK(commits == 1);
  CHECK(!editor.isOpen());
  editor.open({}, [&](std::string) { commits++; });
  CHECK(editor.offerInput(key('g', true)));
  CHECK(commits == 1);
  CHECK(!editor.isOpen());
  CHECK(editor.consumeRedrawRequest());  // closing leaves the blanked field to redraw
}

TEST(inlineeditor_other_keys_are_taken_while_open) {
  StyleProvider styles;
  CellBuffer b(1, 12);
  BufferPlane root(b, styles);
  InlineEditor editor(root);
  editor.open({}, [](std::string) { });
  CHECK(editor.offerInput(key('x')));
  editor.cancel();
  CHECK(!editor.isOpen());
  editor.cancel();  // a no-op when closed
}
