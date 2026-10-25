#pragma once

#include <cellui/Backdrop.h>
#include <cellui/BlendMode.h>
#include <cellui/CellBuffer.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace cellui {

// What a backdrop covers when the display applies it itself (see
// DisplayInfo::native_backdrop): the cells composed below it, and what to
// do to them over the cells it covers. The frame's own cells are then only
// what lies above the backdrop, blank where nothing is.
//
// The same layer holds a plane that is scrolled by a fraction of a cell
// (Plane::setScrollOffset()) when the display scrolls smoothly: `cells` are
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
  BlendMode blend = BlendMode::NORMAL;
  // Stands this high above its plane, casting no shadow, so a lower one's
  // shadow does not fall on it (a cursor, a playhead).
  float lift = 0.0f;
  // A border drawn inside the rectangle, `border_px` pixels wide, over the
  // fill; only where the display draws bands by parts of a cell (the cell
  // version has no room for one between two cells).
  Color border_color = Color(255, 255, 255);
  float border_alpha = 1.0f;
  double border_px = 0;
  // Cells from the plane's left where the border is left out along the top and
  // bottom, and at a side the span reaches (something drawn there stands alone).
  double border_skip_col = 0, border_skip_cols = 0;
};

// A rectangle in pixels.
struct BandBorderRect {
  double x, y, w, h;
};

// The pieces of a band's border in pixels, `x`/`y` being where the band's
// rectangle starts and `skip_x` where its skipped span does.
inline std::vector<BandBorderRect> bandBorderRects(const Band & band, double x, double y, double skip_x, double cell_w, double cell_h) {
  std::vector<BandBorderRect> out;
  double w = band.cols * cell_w, h = band.rows * cell_h;
  double t = std::min(band.border_px, std::min(w, h) / 2);
  if (t <= 0) return out;
  double skip_end = skip_x + band.border_skip_cols * cell_w;
  bool skips = band.border_skip_cols > 0;
  auto edge = [&](double at) {
    double lo = x, hi = x + w;
    if (!skips) {
      out.push_back({ lo, at, w, t });
      return;
    }
    if (skip_x > lo) out.push_back({ lo, at, std::min(skip_x, hi) - lo, t });
    if (skip_end < hi) out.push_back({ std::max(skip_end, lo), at, hi - std::max(skip_end, lo), t });
  };
  edge(y);
  edge(y + h - t);
  double eps = 0.01 * cell_w;
  if (!(skips && skip_x <= x + eps && skip_end > x)) out.push_back({ x, y + t, t, h - 2 * t });
  if (!(skips && skip_end >= x + w - eps && skip_x < x + w)) out.push_back({ x + w - t, y + t, t, h - 2 * t });
  return out;
}

struct ScrollContent {
  // The plane's cells, one row and one column more than it shows.
  CellBuffer cells{ 0, 0 };
  // Where the plane is on the screen, and how much of its content shows.
  int row = 0, col = 0, rows = 0, cols = 0;
  // The content is drawn this many cells up and to the left, each 0 up to 1.
  double shift_rows = 0, shift_cols = 0;
  // Drawn over the content, inside the plane, not shifted.
  std::optional<Band> band;
  // The plane's height.
  float elevation = 0.0f;
  // The plane is laid over what is below with this blend, `blend_alpha` of the way.
  BlendMode blend = BlendMode::NORMAL;
  float blend_alpha = 1.0f;
  bool blended() const { return blend != BlendMode::NORMAL || blend_alpha < 1.0f; }
};

struct BackdropLayer {
  CellBuffer cells;
  Backdrop backdrop;
  int row = 0, col = 0, rows = 0, cols = 0;
  std::optional<ScrollContent> scroll;
};

}  // namespace cellui
