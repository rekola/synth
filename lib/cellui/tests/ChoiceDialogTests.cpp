#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/ChoiceDialog.h>
#include <cellui/InputDispatcher.h>

#include <chrono>
#include <thread>

using namespace cellui;

namespace {

KeyDown key(int id, bool ctrl = false) {
  return InputDispatcher::keyDownFor(Input(id, 0, 0, false, false, ctrl, false, Input::Kind::PRESS));
}

// A 10 x 40 screen with a three-entry dialog on it: 8 rows by 32 columns,
// at row 1, column 4; the entries are on rows 3-5.
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

TEST(choicedialog_draws_title_entries_marker_and_hint_without_a_border) {
  Fixture f;
  CHECK(f.dialog.isOpen());
  CHECK(f.buffer.toPlainText() ==
        "\n\n"
        "      Pick\n"
        "      ● alpha\n"
        "        beta\n"
        "        gamma\n"
        "\n"
        "      Enter: choose  Esc: cancel\n");
  CHECK(f.buffer.toPlainText().find("│") == std::string::npos);
}

TEST(choicedialog_title_is_bold) {
  Fixture f;
  CHECK(f.buffer.at(2, 6).bold);
  CHECK(!f.buffer.at(3, 6).bold);
}

TEST(choicedialog_keys_move_choose_and_cancel) {
  Fixture f;
  using R = ChoiceDialog::Result;
  CHECK(f.dialog.key(key(cellui::Key::Down)) == R::NONE);
  CHECK(f.dialog.selected() == 1);
  CHECK(f.dialog.key(key('n', true)) == R::NONE);
  CHECK(f.dialog.selected() == 2);
  CHECK(f.dialog.key(key(cellui::Key::Down)) == R::NONE);
  CHECK(f.dialog.selected() == 2);  // stops at the end
  CHECK(f.dialog.key(key(cellui::Key::Home)) == R::NONE);
  CHECK(f.dialog.selected() == 0);
  CHECK(f.dialog.key(key(cellui::Key::End)) == R::NONE);
  CHECK(f.dialog.selected() == 2);
  CHECK(f.dialog.key(key(cellui::Key::Enter)) == R::CHOSEN);
  CHECK(f.dialog.key(key(cellui::Key::Escape)) == R::CANCELLED);
  CHECK(f.dialog.key(key('q')) == R::CANCELLED);
  CHECK(f.dialog.key(key('g', true)) == R::CANCELLED);
}

TEST(choicedialog_click_on_an_entry_chooses_it_outside_cancels_padding_is_inert) {
  Fixture f;
  using R = ChoiceDialog::Result;
  CHECK(f.dialog.click(4, 10) == R::CHOSEN);  // second entry
  CHECK(f.dialog.selected() == 1);
  CHECK(f.dialog.click(0, 0) == R::CANCELLED);
  CHECK(f.dialog.click(2, 10) == R::NONE);  // the title
}

TEST(choicedialog_long_lists_scroll_and_show_it_on_the_edge) {
  StyleProvider styles;
  CellBuffer buffer(10, 40);
  BufferPlane root(buffer, styles);
  ChoiceDialog dialog;
  std::vector<std::string> labels;
  for (int i = 1; i <= 10; i++) labels.push_back("item " + std::to_string(i));
  dialog.open("Many", labels, 0);
  dialog.show(root, 10, 40);  // room for 3 entries
  root.refresh();
  CHECK(buffer.toPlainText().find("item 3") != std::string::npos);
  CHECK(buffer.toPlainText().find("item 4") == std::string::npos);
  CHECK(buffer.toPlainText().find("▼") != std::string::npos);
  dialog.key(key(cellui::Key::End));
  dialog.show(root, 10, 40);
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
  CHECK(f.dialog.key(key('x')) == ChoiceDialog::Result::CANCELLED);
}

TEST(choicedialog_smooth_list_scrolls_under_a_cursor_band_that_stays_at_the_edge) {
  StyleProvider styles;
  CellBuffer buffer(10, 40);
  BufferPlane root(buffer, styles);
  root.setSmoothScroll(true);
  ChoiceDialog dialog;
  std::vector<std::string> labels;
  for (int i = 1; i <= 20; i++) labels.push_back("item " + std::to_string(i));
  dialog.open("Many", labels, 0);  // room for 3 entries
  auto show = [&]() {
    dialog.show(root, 10, 40);
    root.refresh();
    return root.takeBackdropLayers();
  };
  auto band_row = [&](const std::vector<BackdropLayer> & layers) {
    for (auto & layer : layers) {
      if (layer.scroll && layer.scroll->band) return layer.scroll->band->row;
    }
    return -1.0;
  };
  show();
  // Down past the bottom of the window: the band reaches the last row and stays there while the list scrolls.
  for (int i = 0; i < 6; i++) dialog.key(key(cellui::Key::Down));
  bool shifted = false;
  double edge = -1;
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    auto layers = show();
    for (auto & layer : layers) {
      if (layer.scroll && layer.scroll->shift_rows > 0 && layer.scroll->shift_rows < 1) shifted = true;
    }
    if (!dialog.animating()) edge = band_row(layers);
  }
  CHECK(shifted);
  CHECK(!dialog.animating());
  CHECK(edge == 2.0);  // the last of the three rows
  CHECK(dialog.selected() == 6);
}
