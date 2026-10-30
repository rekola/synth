#pragma once

#include <cellui/Backdrop.h>
#include <cellui/BlendMode.h>
#include <cellui/CellBuffer.h>

#include <algorithm>
#include <array>
#include <optional>
#include <span>
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
  // The radius in pixels of the border's outer corners (2 softens the corner
  // pixel, 3 clears it; a cell has no room for more). A corner is round only
  // where the border runs on both sides of it, and is square if the border's
  // edge there is too short to hold the arc. The cells under the band stay
  // square, so what fills one to its corner (a full block) shows through the
  // notch: this is for cells of text.
  double border_radius = 0;
};

// A rectangle in pixels.
struct BandBorderRect {
  double x, y, w, h;
};

enum class BandCorner { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT };

// A round corner of a band's border: the `size` x `size` square at (x, y) that
// the arc fills, where the straight pieces stop short of it.
struct BandBorderCorner {
  BandCorner which;
  double x, y, size;
};

// A band's border in pixels: straight pieces and the round corners between them.
struct BandBorderShape {
  std::vector<BandBorderRect> rects;
  std::vector<BandBorderCorner> corners;
  double thickness = 0;
};

// The pieces of a band's border in pixels, `x`/`y` being where the band's
// rectangle starts and `skip_x` where its skipped span does.
inline BandBorderShape bandBorder(const Band & band, double x, double y, double skip_x, double cell_w, double cell_h) {
  BandBorderShape out;
  double w = band.cols * cell_w, h = band.rows * cell_h;
  double t = std::min(band.border_px, std::min(w, h) / 2);
  if (t <= 0) return out;
  out.thickness = t;
  double skip_end = skip_x + band.border_skip_cols * cell_w;
  bool skips = band.border_skip_cols > 0;
  double eps = 0.01 * cell_w;
  bool left_side = !(skips && skip_x <= x + eps && skip_end > x);
  bool right_side = !(skips && skip_end >= x + w - eps && skip_x < x + w);
  // The runs the top and bottom edges are cut into by the skipped span.
  struct Run {
    double a, b;
    bool round_left = false, round_right = false;
  };
  std::array<Run, 2> runs{};
  size_t run_count = 0;
  if (!skips) {
    runs[run_count++] = { x, x + w };
  } else {
    if (skip_x > x) runs[run_count++] = { x, std::min(skip_x, x + w) };
    if (skip_end < x + w) runs[run_count++] = { std::max(skip_end, x), x + w };
  }
  // At most half the height, so the two corners of a side meet at most.
  double r = std::max(0.0, std::min(band.border_radius, h / 2));
  bool round_left = false, round_right = false;
  for (Run & run : std::span(runs).first(run_count)) {
    if (r <= 0) break;
    bool l = left_side && run.a <= x + eps, rr = right_side && run.b >= x + w - eps;
    if (run.b - run.a < (l ? r : 0.0) + (rr ? r : 0.0)) l = rr = false;
    run.round_left = l;
    run.round_right = rr;
    round_left |= l;
    round_right |= rr;
  }
  for (double at : { y, y + h - t }) {
    for (const Run & run : std::span(runs).first(run_count)) {
      double a = run.a + (run.round_left ? r : 0.0), b = run.b - (run.round_right ? r : 0.0);
      if (b > a) out.rects.push_back({ a, at, b - a, t });
    }
  }
  double top = y + (round_left ? r : t), bottom = y + h - (round_left ? r : t);
  if (left_side && bottom > top) out.rects.push_back({ x, top, t, bottom - top });
  top = y + (round_right ? r : t);
  bottom = y + h - (round_right ? r : t);
  if (right_side && bottom > top) out.rects.push_back({ x + w - t, top, t, bottom - top });
  if (round_left) {
    out.corners.push_back({ BandCorner::TOP_LEFT, x, y, r });
    out.corners.push_back({ BandCorner::BOTTOM_LEFT, x, y + h - r, r });
  }
  if (round_right) {
    out.corners.push_back({ BandCorner::TOP_RIGHT, x + w - r, y, r });
    out.corners.push_back({ BandCorner::BOTTOM_RIGHT, x + w - r, y + h - r, r });
  }
  return out;
}

// The same with square corners.
inline std::vector<BandBorderRect> bandBorderRects(const Band & band, double x, double y, double skip_x, double cell_w, double cell_h) {
  Band square = band;
  square.border_radius = 0;
  return bandBorder(square, x, y, skip_x, cell_w, cell_h).rects;
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
