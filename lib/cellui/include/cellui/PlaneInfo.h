#pragma once

#include <cellui/BackdropLayer.h>
#include <cellui/MouseCursor.h>

#include <memory>
#include <string>
#include <vector>

namespace cellui {

class Widget;

// What a plane is, as a test or an inspector sees it: its place inside its
// parent, its size, whether it shows, and the planes on it, bottom to top.
struct PlaneInfo {
  std::string name;  // set by the application (Plane::setName()), may be empty
  int y = 0, x = 0;  // relative to the parent
  int rows = 0, cols = 0;
  bool visible = true;
  float elevation = 0;  // above the parent (Plane::setElevation())
  MouseCursor mouse_cursor = MouseCursor::INHERIT;  // Plane::setMouseCursor()
  bool reader = false;  // a text field is open on this plane
  bool picker = false;  // a picker is open on this plane
  std::vector<PlaneInfo> children;
};

// The part of the screen a plane above the base covers, at its height above
// it (its own and its ancestors' elevations together).
struct ElevatedArea {
  int row = 0, col = 0, rows = 0, cols = 0;
  float elevation = 0;
  std::string name;
  // The layer its cells were composed into (the frame's own cells are one
  // past the last layer): a backdrop at or above that layer covers it.
  size_t layer = 0;
  // False for lifted cells that only stand above lower shadows and cast none.
  bool shadow = true;
};

// The part of the screen a plane asked a pointer shape for.
struct CursorArea {
  int row = 0, col = 0, rows = 0, cols = 0;
  MouseCursor cursor = MouseCursor::DEFAULT;
};

// The part of the screen a plane covers, with the widget that owns the plane:
// what the input dispatcher tests a pointer position against. `life` expires
// with the widget, so an area from an older frame never names a dead one.
struct HitArea {
  int row = 0, col = 0, rows = 0, cols = 0;
  Widget * widget = nullptr;
  std::weak_ptr<void> life;
};

// What an application tells a backend about a frame besides its cells.
struct FrameInfo {
  PlaneInfo planes;
  // Which widget takes the keys, in the application's own words.
  std::string focus;
  // The layers below each backdrop, bottom first. The display applies the
  // backdrops (blurs and dims the layer below); the frame's cells are what
  // is above the last one. Always given, empty without a backdrop.
  std::vector<BackdropLayer> backdrops;
  // Every shown plane above the base, in the order they are drawn (lowest
  // first). Always given.
  std::vector<ElevatedArea> elevations;
  // The areas planes asked a pointer shape for, in the order they are drawn
  // (lowest first): the last one over a cell sets its shape, and a cell none
  // covers has the arrow. Always given.
  std::vector<CursorArea> mouse_cursors;
};

// The pointer's shape over a cell.
inline MouseCursor mouseCursorAt(const std::vector<CursorArea> & areas, int row, int col) {
  for (auto a = areas.rbegin(); a != areas.rend(); ++a) {
    if (row >= a->row && row < a->row + a->rows && col >= a->col && col < a->col + a->cols) return a->cursor;
  }
  return MouseCursor::DEFAULT;
}

}  // namespace cellui
