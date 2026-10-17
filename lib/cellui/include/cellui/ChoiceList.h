#pragma once

#include <cellui/Viewport.h>

#include <algorithm>

// The selection and scroll position of a list a person picks one entry from -
// the part of a choice dialog that doesn't depend on any toolkit. Movement
// stops at the ends rather than wrapping, since the first and last entries of
// a long list are the ones people overshoot to.
namespace cellui {

class ChoiceList {
 public:
  void reset(int count, int selected) {
    count_ = std::max(0, count);
    window_ = ScrollAxis();
    selected_ = 0;
    select(selected);
  }

  int count() const { return count_; }
  int selected() const { return selected_; }

  void select(int index) {
    selected_ = count_ == 0 ? 0 : std::clamp(index, 0, count_ - 1);
  }
  void move(int delta) { select(selected_ + delta); }
  void home() { select(0); }
  void end() { select(count_ - 1); }

  // Index of the first entry shown in a window of `rows` rows. Scrolls only
  // as far as needed to keep the selection in view, so the window doesn't
  // jump while the selection moves inside it.
  int firstVisible(int rows) {
    window_.setExtent(count_, std::max(1, rows));
    window_.reveal(selected_);
    return window_.offset();
  }

  // Scrolls the window without moving the selection (the mouse wheel); the
  // selection is pulled into view only if the scroll would leave it outside.
  void scroll(int delta, int rows) {
    window_.setExtent(count_, std::max(1, rows));
    window_.scrollBy(delta);
    selected_ = count_ == 0 ? 0 : std::clamp(selected_, window_.offset(), std::min(count_ - 1, window_.offset() + window_.view() - 1));
  }

  // Where the display scrolls smoothly the window follows its offset with an
  // animation (see ScrollAxis): firstShown() and fraction() say where it is.
  void setSmooth(bool smooth) { window_.setSmooth(smooth); }
  bool smooth() const { return window_.smooth(); }
  // The window jumps to its offset (a list just opened).
  void snap() {
    bool smooth = window_.smooth();
    window_.setSmooth(false);
    window_.setSmooth(smooth);
  }
  void tick(double seconds) { window_.tick(seconds); }
  bool animating() const { return window_.animating(); }
  int offset() const { return window_.offset(); }
  int firstShown() const { return window_.firstShown(); }
  double fraction() const { return window_.fraction(); }
  // The entry under row `row` of the window, as drawn now.
  int indexAt(int row) const { return window_.indexAt(row); }

 private:
  int count_ = 0;
  int selected_ = 0;
  ScrollAxis window_;
};

}  // namespace cellui
