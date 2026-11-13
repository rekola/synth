#pragma once

#include <cellui/Color.h>

#include <cstdint>

namespace cellui {

// Edges of a cell a border is drawn on.
enum CellEdge : uint8_t { EDGE_TOP = 1, EDGE_BOTTOM = 2, EDGE_LEFT = 4, EDGE_RIGHT = 8 };

// A 1-pixel line along some edges of one screen cell, drawn over the cell by a display that has
// pictures (the terminal, as kitty images). A band's border (Band::border_px) is composed into
// these on a display that draws bands by whole cells; the colour is drawn opaque.
struct CellBorder {
  int row = 0, col = 0;
  uint8_t edges = 0;  // CellEdge bits
  Color color;
};

}  // namespace cellui
