#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/SpinBox.h>

using namespace cellui;

namespace {

Input wheel(int id) { return Input(id, 0, 0, false, false, false, false, Input::Kind::PRESS); }
Input clickAt(int y, int x) { return Input(cellui::Key::Button1, y, x, false, false, false, false, Input::Kind::RELEASE); }
Input typed(int id) { return Input(id, 0, 0, false, false, false, false, Input::Kind::PRESS); }

struct Fixture {
  StyleProvider styles;
  CellBuffer buffer{3, 30};
  BufferPlane root{buffer, styles};
  int value = 4;
  SpinBox spin{root, "Octave", 0, 8, [this] { return value; }, [this](int v) { value = v; }, Color("#202020"), Color("#c0c0c0")};
  int minus_x = -1, plus_x = -1, field_x = -1;
  Fixture() {
    spin.resize(1, spin.preferredWidth());
    spin.move(1, 2);
    spin.render(styles, true);
    root.refresh();
    // Find the cells by probing: the arrows are hit-tested, the field is where
    // a click starts editing.
    for (int x = 2; x < 2 + spin.preferredWidth(); x++) {
      if (spin.hitsArrow(1, x)) {
        if (minus_x < 0) minus_x = x;
        plus_x = x;
      }
    }
    field_x = (minus_x + plus_x) / 2;
  }
};

}  // namespace

TEST(spinbox_shows_label_arrows_and_value) {
  Fixture f;
  CHECK(f.buffer.toPlainText() == "\n  Octave ◂ 4 ▸\n");
}

TEST(spinbox_width_fits_the_widest_value) {
  Fixture f;
  CHECK(f.spin.preferredWidth() == 13);
}

TEST(spinbox_render_only_redraws_when_something_changed) {
  Fixture f;
  CHECK(!f.spin.render(f.styles, false));
  f.value = 5;
  CHECK(f.spin.render(f.styles, false));
  f.root.refresh();
  CHECK(f.buffer.toPlainText() == "\n  Octave ◂ 5 ▸\n");
}

TEST(spinbox_arrow_clicks_and_wheel_step_within_the_range) {
  Fixture f;
  CHECK(f.spin.offerInput(clickAt(1, f.plus_x)));
  CHECK(f.value == 5);
  CHECK(f.spin.offerInput(clickAt(1, f.minus_x)));
  CHECK(f.spin.offerInput(clickAt(1, f.minus_x)));
  CHECK(f.value == 3);
  CHECK(f.spin.offerInput(wheel(cellui::Key::Button4)));
  CHECK(f.value == 4);
  CHECK(f.spin.offerInput(wheel(cellui::Key::Button5)));
  CHECK(f.spin.offerInput(wheel(cellui::Key::Button5)));
  CHECK(f.value == 2);
  f.value = 8;
  f.spin.offerInput(wheel(cellui::Key::Button4));
  CHECK(f.value == 8);  // clamped at the maximum
}

TEST(spinbox_pressing_an_arrow_is_pending_until_release) {
  Fixture f;
  Input press(cellui::Key::Button1, 1, f.plus_x, false, false, false, false, Input::Kind::PRESS);
  CHECK(f.spin.offerInput(press));
  CHECK(f.spin.arrowPressPending());
  CHECK(f.value == 4);
  CHECK(f.spin.offerInput(clickAt(1, f.plus_x)));
  CHECK(!f.spin.arrowPressPending());
  CHECK(f.value == 5);
}

TEST(spinbox_typing_a_value_commits_when_no_digit_could_follow) {
  Fixture f;
  CHECK(!f.spin.isEditing());
  CHECK(!f.spin.offerInput(typed('5')));  // not editing: left to the app
  CHECK(f.spin.offerInput(clickAt(1, f.field_x)));
  CHECK(f.spin.isEditing());
  CHECK(f.spin.offerInput(typed('7')));  // 70 > 8, so 7 is final
  CHECK(!f.spin.isEditing());
  CHECK(f.value == 7);
}

TEST(spinbox_escape_cancels_an_edit_and_other_keys_end_it) {
  Fixture f;
  f.spin.offerInput(clickAt(1, f.field_x));
  CHECK(f.spin.offerInput(typed(cellui::Key::Escape)));
  CHECK(!f.spin.isEditing());
  CHECK(f.value == 4);
  f.spin.offerInput(clickAt(1, f.field_x));
  CHECK(!f.spin.offerInput(typed('x')));  // ended, and left for the app
  CHECK(!f.spin.isEditing());
}
