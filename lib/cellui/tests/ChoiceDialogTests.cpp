#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/ChoiceDialog.h>

using namespace cellui;

namespace {

Input key(int id, bool ctrl = false) {
  return Input(id, 0, 0, false, false, ctrl, false, Input::Kind::PRESS);
}
Input click(int y, int x) { return Input(cellui::Key::Button1, y, x, false, false, false, false, Input::Kind::RELEASE); }

// A 10 x 40 screen with a three-entry dialog on it: 5 rows by 32 columns,
// at row 2, column 4.
struct Fixture {
  StyleProvider styles;
  CellBuffer buffer{10, 40};
  BufferPlane root{buffer, styles};
  ChoiceDialog dialog;
  Fixture() {
    dialog.open("Pick", {"alpha", "beta", "gamma"}, 0);
    dialog.show(root, 10, 40);
    root.refresh();
  }
};

}  // namespace

TEST(choicedialog_draws_title_entries_marker_and_hint) {
  Fixture f;
  CHECK(f.dialog.isOpen());
  CHECK(f.buffer.toPlainText() ==
        "\n\n"
        "    ┌─ Pick ───────────────────────┐\n"
        "    │ ● alpha                      │\n"
        "    │   beta                       │\n"
        "    │   gamma                      │\n"
        "    └─ Enter: choose  Esc: cancel ─┘\n");
}

TEST(choicedialog_keys_move_choose_and_cancel) {
  Fixture f;
  using R = ChoiceDialog::Result;
  CHECK(f.dialog.offerInput(key(cellui::Key::Down)) == R::NONE);
  CHECK(f.dialog.selected() == 1);
  CHECK(f.dialog.offerInput(key('n', true)) == R::NONE);
  CHECK(f.dialog.selected() == 2);
  CHECK(f.dialog.offerInput(key(cellui::Key::Down)) == R::NONE);
  CHECK(f.dialog.selected() == 2);  // stops at the end
  CHECK(f.dialog.offerInput(key(cellui::Key::Home)) == R::NONE);
  CHECK(f.dialog.selected() == 0);
  CHECK(f.dialog.offerInput(key(cellui::Key::End)) == R::NONE);
  CHECK(f.dialog.selected() == 2);
  CHECK(f.dialog.offerInput(key(cellui::Key::Enter)) == R::CHOSEN);
  CHECK(f.dialog.offerInput(key(cellui::Key::Escape)) == R::CANCELLED);
  CHECK(f.dialog.offerInput(key('q')) == R::CANCELLED);
  CHECK(f.dialog.offerInput(key('g', true)) == R::CANCELLED);
}

TEST(choicedialog_key_releases_do_nothing) {
  Fixture f;
  Input release(cellui::Key::Enter, 0, 0, false, false, false, false, Input::Kind::RELEASE);
  CHECK(f.dialog.offerInput(release) == ChoiceDialog::Result::NONE);
}

TEST(choicedialog_click_on_an_entry_chooses_it_outside_cancels_border_is_inert) {
  Fixture f;
  using R = ChoiceDialog::Result;
  CHECK(f.dialog.offerInput(click(4, 10)) == R::CHOSEN);  // second entry
  CHECK(f.dialog.selected() == 1);
  CHECK(f.dialog.offerInput(click(0, 0)) == R::CANCELLED);
  CHECK(f.dialog.offerInput(click(2, 10)) == R::NONE);  // the top border
  Input press(cellui::Key::Button1, 4, 10, false, false, false, false, Input::Kind::PRESS);
  CHECK(f.dialog.offerInput(press) == R::NONE);  // resolved on release
}

TEST(choicedialog_long_lists_scroll_and_show_it_on_the_edge) {
  StyleProvider styles;
  CellBuffer buffer(8, 40);
  BufferPlane root(buffer, styles);
  ChoiceDialog dialog;
  std::vector<std::string> labels;
  for (int i = 1; i <= 10; i++) labels.push_back("item " + std::to_string(i));
  dialog.open("Many", labels, 0);
  dialog.show(root, 8, 40);  // room for 4 entries
  root.refresh();
  CHECK(buffer.toPlainText().find("item 4") != std::string::npos);
  CHECK(buffer.toPlainText().find("item 5") == std::string::npos);
  CHECK(buffer.toPlainText().find("▼") != std::string::npos);
  dialog.offerInput(key(cellui::Key::End));
  dialog.show(root, 8, 40);
  root.refresh();
  CHECK(buffer.toPlainText().find("item 10") != std::string::npos);
  CHECK(buffer.toPlainText().find("▲") != std::string::npos);
}

TEST(choicedialog_long_label_ends_in_an_ellipsis) {
  StyleProvider styles;
  CellBuffer buffer(8, 20);
  BufferPlane root(buffer, styles);
  ChoiceDialog dialog;
  dialog.open("T", {"a label far wider than the screen allows"}, -1);
  dialog.show(root, 8, 20);
  root.refresh();
  CHECK(buffer.toPlainText().find("\xe2\x80\xa6") != std::string::npos);
}

TEST(choicedialog_close_removes_it_and_empty_dialog_cancels) {
  Fixture f;
  f.dialog.close();
  f.root.refresh();
  CHECK(!f.dialog.isOpen());
  CHECK(f.buffer.toPlainText().empty());
  CHECK(f.dialog.offerInput(key('x')) == ChoiceDialog::Result::CANCELLED);
}
