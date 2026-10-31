#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/CellBuffer.h>
#include <cellui/Input.h>
#include <cellui/PickerPopup.h>
#include <cellui/StyleProvider.h>

using namespace cellui;

TEST(picker_popup_lists_and_wraps) {
  StyleProvider styles;
  CellBuffer b(10, 20);
  BufferPlane root(b, styles);
  PickerPopup picker;
  picker.open(root, 1, 2, 5, 12, { "alpha", "beta", "gamma" }, 0);
  root.refresh();
  CHECK(b.toPlainText() == "\n\n    alpha\n     beta\n    gamma\n");
  CHECK(b.at(2, 2).bg && b.at(1, 2).bg);  // opaque, padding rows included
  CHECK(picker.selection() == "alpha");
  CHECK(picker.key(Key::Up, false) == PickerPopup::Result::MOVED);
  CHECK(picker.selection() == "gamma");
  CHECK(picker.wheel(1) == PickerPopup::Result::MOVED);
  CHECK(picker.selection() == "alpha");
  // A click on a row chooses it; Enter chooses; Ctrl-G cancels.
  CHECK(picker.click(3, 5) == PickerPopup::Result::CHOSEN);
  CHECK(picker.selection() == "beta");
  CHECK(picker.click(8, 5) == PickerPopup::Result::CANCELLED);
  CHECK(picker.key(Key::Enter, false) == PickerPopup::Result::CHOSEN);
  CHECK(picker.key('g', true) == PickerPopup::Result::CANCELLED);
  CHECK(picker.key('x', false) == PickerPopup::Result::IGNORED);
  picker.close();
  CHECK(!picker.isOpen());
}
