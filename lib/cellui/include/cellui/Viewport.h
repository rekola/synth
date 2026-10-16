#pragma once

#include <algorithm>

// Which part of a larger content a widget shows, and the rules every
// scrolling widget shares: the offset stays inside the content, the wheel
// moves the view and not the cursor (Shift: sideways) and leaves it
// detached from the cursor until the cursor moves, and a cursor is kept a
// margin from the edges. A widget states its content size and its own size
// and draws the part it is given; where a widget is special (the pattern
// editor's sideways scrolling) it keeps that policy and only uses the axis.
//
// Offsets are whole cells. A backend that scrolls by fractions of a cell
// will take the fractional part from the same offset.
namespace cellui {

// One dimension.
class ScrollAxis {
 public:
  int offset() const { return offset_; }
  int content() const { return content_; }
  int view() const { return view_; }
  // How far the content can scroll: its overhang past the view.
  int maxOffset() const { return std::max(0, content_ - view_); }
  // The offset after the last cell of the content is in view.
  bool atEnd() const { return offset_ >= maxOffset(); }

  // The content and the view have a size (cells); the offset is brought
  // back inside the content, so a shrunk content never leaves a stale one.
  void setExtent(int content, int view) {
    content_ = std::max(0, content);
    view_ = std::max(0, view);
    offset_ = std::clamp(offset_, 0, maxOffset());
  }

  void scrollTo(int offset) { offset_ = std::clamp(offset, 0, maxOffset()); }
  void scrollBy(int delta) { scrollTo(offset_ + delta); }

  // Slides the view just far enough that `index` is at least `margin` cells
  // from both edges, and no further, so it only moves when the cursor
  // pushed it out. The margin is capped to what the view can hold, and an
  // end of the content stops the slide.
  void reveal(int index, int margin = 0) {
    margin = std::clamp(margin, 0, std::max(0, (view_ - 1) / 2));
    reveal(index, margin, margin);
  }
  // The same with different margins at the two ends, for a widget that draws
  // over the last rows of its view (a button bar): `before` cells above
  // `index` and `after` below it stay in view, cut down together to what
  // the view can hold.
  void reveal(int index, int before, int after) {
    if (view_ <= 0) return;
    before = std::clamp(before, 0, view_ - 1);
    after = std::clamp(after, 0, view_ - 1 - before);
    if (index - before < offset_) offset_ = index - before;
    if (index + after >= offset_ + view_) offset_ = index + after - view_ + 1;
    offset_ = std::clamp(offset_, 0, maxOffset());
  }

  // Whether `index` is inside the view.
  bool shows(int index) const { return index >= offset_ && index < offset_ + view_; }

 private:
  int offset_ = 0;
  int content_ = 0;
  int view_ = 0;
};

// Two dimensions, with the detached state the wheel sets.
class Viewport {
 public:
  ScrollAxis rows;
  ScrollAxis cols;

  void setExtent(int content_rows, int content_cols, int view_rows, int view_cols) {
    rows.setExtent(content_rows, view_rows);
    cols.setExtent(content_cols, view_cols);
  }

  // The mouse wheel: `direction` is -1 (up) or 1 (down), in steps of the
  // given size. It moves the view and detaches it, and never the cursor.
  void wheel(int direction, bool sideways, int step = 1) {
    if (sideways) cols.scrollBy(direction * step);
    else rows.scrollBy(direction * step);
    detached_ = true;
  }

  // Whether the view was scrolled by hand and so follows neither the cursor
  // nor a playhead; attach() when the cursor next moves.
  bool detached() const { return detached_; }
  void attach() { detached_ = false; }

 private:
  bool detached_ = false;
};

}  // namespace cellui
