#pragma once

#include <cellui/Backdrop.h>
#include <cellui/CellBuffer.h>

namespace cellui {

// What a backdrop covers when the display applies it itself (see
// DisplayInfo::native_backdrop): the cells composed below it, and what to
// do to them over the cells it covers. The frame's own cells are then only
// what lies above the backdrop, blank where nothing is.
struct BackdropLayer {
  CellBuffer cells;
  Backdrop backdrop;
  int row = 0, col = 0, rows = 0, cols = 0;
};

}  // namespace cellui
