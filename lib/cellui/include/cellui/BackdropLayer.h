#pragma once

#include <cellui/Backdrop.h>
#include <cellui/CellBuffer.h>

#include <optional>

namespace cellui {

// What a backdrop covers when the display applies it itself (see
// DisplayInfo::native_backdrop): the cells composed below it, and what to
// do to them over the cells it covers. The frame's own cells are then only
// what lies above the backdrop, blank where nothing is.
//
// The same layer holds a plane that is scrolled by a fraction of a cell
// (UIPlane::setScrollOffset()) when the display scrolls smoothly: `cells` are
// what is composed below it, `scroll` the plane's own, and `backdrop` is
// then inactive.
// A translucent rectangle over a scrolled plane, at a fractional position in
// the plane's own cells (it does not shift with the content): a playhead that
// moves smoothly over rows that scroll.
struct Band {
  double row = 0, rows = 0;  // cells from the plane's top, and how tall
  double col = 0, cols = 0;  // the same, in cells from the plane's left
  Color color = Color(255, 255, 255);
  float alpha = 1.0f;
};

struct ScrollContent {
  // The plane's cells, one row and one column more than it shows.
  CellBuffer cells{ 0, 0 };
  // Where the plane is on the screen, and how much of its content shows.
  int row = 0, col = 0, rows = 0, cols = 0;
  // The content is drawn this many cells up and to the left, each 0 up to 1.
  double shift_rows = 0, shift_cols = 0;
  // Drawn over the content, inside the plane, not shifted.
  std::optional<Band> band;
};

struct BackdropLayer {
  CellBuffer cells;
  Backdrop backdrop;
  int row = 0, col = 0, rows = 0, cols = 0;
  std::optional<ScrollContent> scroll;
};

}  // namespace cellui
