#ifndef _SPINBOX_H_
#define _SPINBOX_H_

#include "../UIElement.h"
#include "../StyleProvider.h"
#include "../../model/Color.h"
#include "../../playback/InputEvent.h"
#include "../../util/digit.h"
#include "../../util/Utf8.h"

#include <algorithm>
#include <functional>
#include <string>

#include <fmt/core.h>

// A small, reusable inline numeric stepper: an optional label, a small
// left-arrow button, a directly-editable value field, and a small right-
// arrow button, all mouse-clickable - a compact toolbar-style spinner,
// sized to disappear into a single line (e.g. InfoLine) rather than
// needing its own bordered box. The arrows and the value field all share
// one dark chip look (see render()) rather than the arrows standing out
// as separate bright buttons.
// Owns none of the value itself (only get_value/set_value callbacks), so
// the same class backs Controller::getGlobalOctave()/setGlobalOctave()
// today and can back any other bounded integer later without change.
class SpinBox : public UIElement {
 public:
  // bg_color/fg_color are the widget's own idle colors (label text only -
  // the value field always shows dark-on-bright regardless, see render())
  // - passed in rather than pulled from StyleProvider since this widget is
  // meant to blend into whatever bar it's embedded in (e.g. InfoLine's own
  // hardcoded gray-on-dark, not StyleProvider's generic window colors).
  SpinBox(UIPlane & parent, std::string label, int min_value, int max_value,
          std::function<int()> get_value, std::function<void(int)> set_value,
          Color bg_color, Color fg_color)
    : UIElement(parent), label_(std::move(label)), min_value_(min_value), max_value_(max_value),
      get_value_(std::move(get_value)), set_value_(std::move(set_value)),
      bg_color_(bg_color), fg_color_(fg_color) { }

  // Exact column width this widget needs at its current label/min/max -
  // callers size their resize() call from this instead of guessing a
  // round number, so layout and hit-testing can never drift apart.
  int preferredWidth() const { auto c = layoutColumns(); return c.plus_start + buttonWidth(); }

  bool render(const StyleProvider & styles, bool refresh = false) {
    auto value = get_value_();
    if (!refresh && value == current_value_ && editing_ == current_editing_ && edit_buffer_ == current_edit_buffer_) {
      return false;
    }

    auto c = layoutColumns();
    auto cols = getDim().second;

    // Blank the whole row first in the ambient bar colors - otherwise the
    // gap after the label (and anything else render() doesn't explicitly
    // overwrite below) would show through as whatever a freshly created
    // plane's own base cell defaults to (see UIPlane::createChild()),
    // not a blend-in blank.
    setFgColor(fg_color_);
    setBgColor(bg_color_);
    putstr(0, 0, std::string(static_cast<size_t>(std::max(0, cols)), ' '));
    if (!label_.empty()) putstr(0, 0, label_);

    // Arrows: same dark-background/bright-text look as the field's own
    // idle state below, so the whole minus/digit/plus run reads as one
    // chip rather than the arrows standing out as separate buttons. Blank
    // from right after the label (not just from minus_start) so the gap
    // column before the left arrow is part of the dark chip too, rather
    // than showing the ambient bar color between the label and the chip.
    setFgColor(styles.window_accent_fg_color);
    setBgColor(styles.window_bg_color);
    auto chip_width = c.plus_start + buttonWidth() - c.label_width;
    putstr(0, c.label_width, std::string(static_cast<size_t>(std::max(0, chip_width)), ' '));
    putstr(0, c.minus_start, kMinusGlyph);
    putstr(0, c.plus_start, kPlusGlyph);

    if (editing_) {
      setFgColor(styles.highlight_fg_color);
      setBgColor(styles.highlight_bg_color);
    } else {
      // Dark background, bright text - real per-cell transparency isn't
      // something this terminal-cell model offers, so "dark" stands in
      // for it: the field reads as its own cut-out chip against the
      // ambient bar rather than just more label-colored text.
      setFgColor(styles.window_accent_fg_color);
      setBgColor(styles.window_bg_color);
    }
    // One character of padding on each side of the digit - part of the
    // field's own width (see layoutColumns()), filled here via center
    // alignment rather than as separately-drawn blank cells.
    auto field_width = c.field_end - c.field_start;
    putstr(0, c.field_start, fmt::format("{:^{}}", fieldText(), field_width));

    current_value_ = value;
    current_editing_ = editing_;
    current_edit_buffer_ = edit_buffer_;
    return true;
  }

  bool offerInput(const InputEvent & input) override {
    if (input.getId() == NCKEY_BUTTON1) {
      if (input.getKind() != InputEvent::Kind::RELEASE) return true; // resolved on release, matching TerminalMenu's own click convention
      auto [pos_y, pos_x] = getPosition();
      auto [rows, cols] = getDim();
      auto y = input.getY() - pos_y, x = input.getX() - pos_x;
      if (y < 0 || y >= rows || x < 0 || x >= cols) return true; // shouldn't happen - only reached while this is the click's own target
      auto c = layoutColumns();
      if (y == 0 && x >= c.minus_start && x < c.minus_start + buttonWidth()) {
	cancelEditing();
	step(-1);
      } else if (y == 0 && x >= c.plus_start && x < c.plus_start + buttonWidth()) {
	cancelEditing();
	step(1);
      } else if (y == 0 && x >= c.field_start && x < c.field_end) {
	editing_ = true;
	edit_buffer_.clear();
      } else {
	cancelEditing();
      }
      return true;
    } else if (input.getId() == NCKEY_BUTTON4) { // scroll wheel up
      cancelEditing();
      step(1);
      return true;
    } else if (input.getId() == NCKEY_BUTTON5) { // scroll wheel down
      cancelEditing();
      step(-1);
      return true;
    }

    if (!editing_) return false;
    if (input.getKind() == InputEvent::Kind::RELEASE) return true; // consume key-up while editing - nothing to do

    if (input.getId() == NCKEY_ENTER) {
      if (!edit_buffer_.empty()) {
	int parsed = 0;
	for (char ch : edit_buffer_) parsed = parsed * 10 + (ch - '0');
	set_value_(std::clamp(parsed, min_value_, max_value_));
      }
      cancelEditing();
    } else if (input.getId() == NCKEY_ESC) {
      cancelEditing();
    } else if (input.getId() == NCKEY_BACKSPACE) {
      if (!edit_buffer_.empty()) edit_buffer_.pop_back();
    } else {
      auto d = digit(input.getId(), 10);
      if (d >= 0 && static_cast<int>(edit_buffer_.size()) < fieldWidth()) edit_buffer_ += static_cast<char>('0' + d);
    }
    return true;
  }

  bool isEditing() const { return editing_; }

  // Discards any half-typed value without committing it - called both by
  // this widget's own offerInput() (a click on +/-/elsewhere while
  // editing) and by UI's own focus-tracking (a click that moves focus
  // away from this widget entirely) - see UI::offerInput()'s BUTTON1
  // branch. Same "explicit commit or cancel, never commit-on-blur"
  // contract as StatusLine's own reader.
  void cancelEditing() { editing_ = false; edit_buffer_.clear(); }

 private:
  // Small triangular arrows (U+25C2/U+25B8), not the heavy plus/minus this
  // widget used to draw as separate bright chips - their actual column
  // width is confirmed via Utf8::displayWidth() below, not assumed, which
  // is why every column below is computed through buttonWidth() rather
  // than a hardcoded 1.
  static constexpr const char * kMinusGlyph = "◂";
  static constexpr const char * kPlusGlyph = "▸";

  // Single-row layout: "<label><gap><minus><pad><digit><pad><plus>" - the
  // gap after the label and the padding around the digit are both real
  // reserved columns; the buttons themselves need no such gap since the
  // whole minus/pad/digit/pad/plus span shares one dark chip background
  // (see render()).
  struct Columns { int label_width, minus_start, field_start, field_end, plus_start; };

  // Column ranges (half-open), shared by render() and offerInput() so
  // layout and hit-testing can never drift apart.
  Columns layoutColumns() const {
    auto label_width = Utf8::displayWidth(label_);
    auto minus_start = label_width + (label_width > 0 ? 1 : 0); // 1-column gap after a non-empty label
    auto field_start = minus_start + buttonWidth();
    auto field_end = field_start + fieldWidth() + 2; // +2 = one padding column on each side of the digit
    auto plus_start = field_end;
    return { label_width, minus_start, field_start, field_end, plus_start };
  }

  static int buttonWidth() {
    return std::max(Utf8::displayWidth(kMinusGlyph), Utf8::displayWidth(kPlusGlyph));
  }

  int fieldWidth() const {
    auto w = static_cast<int>(std::to_string(max_value_).size());
    if (min_value_ < 0) w = std::max(w, static_cast<int>(std::to_string(min_value_).size()));
    return std::max(w, 1);
  }

  std::string fieldText() const {
    if (editing_ && !edit_buffer_.empty()) return edit_buffer_;
    return std::to_string(get_value_());
  }

  void step(int delta) { set_value_(std::clamp(get_value_() + delta, min_value_, max_value_)); }

  std::string label_;
  int min_value_, max_value_;
  std::function<int()> get_value_;
  std::function<void(int)> set_value_;
  Color bg_color_, fg_color_;

  bool editing_ = false;
  std::string edit_buffer_;

  // Dirty-check cache, same convention InfoLine::current_*_ already uses.
  int current_value_ = 0;
  bool current_editing_ = false;
  std::string current_edit_buffer_;
};

#endif
