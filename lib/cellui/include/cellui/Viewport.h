#pragma once

#include <algorithm>
#include <cmath>

// Which part of a larger content a widget shows, and the rules every
// scrolling widget shares: the offset stays inside the content, the wheel
// moves the view and not the cursor (Shift: sideways) and leaves it
// detached from the cursor until the cursor moves, and a cursor is kept a
// margin from the edges. A widget states its content size and its own size
// and draws the part it is given; where a widget is special (the pattern
// editor's sideways scrolling) it keeps that policy and only uses the axis.
//
// The offset a widget reasons with - what scrollTo(), reveal() and the
// wheel move, and what a click is resolved against - is a whole number of
// cells. Where the display scrolls smoothly the view follows it with an
// animation: shown() is the position now, the widget draws from firstShown()
// with one cell more than the view holds, and the plane shifts that by
// fraction() (UIPlane::setScrollOffset()). Where it does not, shown() is the
// offset itself and fraction() is 0.
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
    shown_ = std::clamp(shown_, 0.0, static_cast<double>(maxOffset()));
    if (!smooth_) shown_ = offset_;
  }

  void scrollTo(int offset) {
    offset_ = std::clamp(offset, 0, maxOffset());
    if (!smooth_) shown_ = offset_;
  }
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
    if (!smooth_) shown_ = offset_;
  }

  // Whether the view follows the offset with an animation, or jumps to it.
  void setSmooth(bool smooth) {
    smooth_ = smooth;
    if (!smooth_) shown_ = offset_;
  }
  bool smooth() const { return smooth_; }
  // Where the view is now, in cells; the first cell it shows and how far
  // into it the view is (0 up to, not including, 1).
  double shown() const { return shown_; }
  int firstShown() const { return static_cast<int>(std::floor(shown_)); }
  double fraction() const { return shown_ - std::floor(shown_); }
  bool animating() const { return shown_ != static_cast<double>(offset_); }
  // Moves the view toward the offset by `seconds` of the animation: it
  // closes the distance exponentially, so it starts fast and settles, with a
  // time constant of kSettleSeconds.
  void tick(double seconds) {
    if (!animating()) return;
    double distance = offset_ - shown_;
    shown_ += distance * (1.0 - std::exp(-std::max(0.0, seconds) / kSettleSeconds));
    if (std::abs(offset_ - shown_) < kSnap) shown_ = offset_;
  }
  static constexpr double kSettleSeconds = 0.06;
  static constexpr double kSnap = 0.02;

  // The index of the cell shown `cell` cells from the top of the view: the
  // one under the middle of that cell, which is what a click is on while the
  // view moves.
  int indexAt(int cell) const { return static_cast<int>(std::floor(shown_ + cell + 0.5)); }

  // Whether `index` is inside the view.
  bool shows(int index) const { return index >= offset_ && index < offset_ + view_; }

 private:
  int offset_ = 0;
  int content_ = 0;
  int view_ = 0;
  double shown_ = 0;
  bool smooth_ = false;
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

  void setSmooth(bool smooth) {
    rows.setSmooth(smooth);
    cols.setSmooth(smooth);
  }
  void tick(double seconds) {
    rows.tick(seconds);
    cols.tick(seconds);
  }
  bool animating() const { return rows.animating() || cols.animating(); }

 private:
  bool detached_ = false;
};

}  // namespace cellui
