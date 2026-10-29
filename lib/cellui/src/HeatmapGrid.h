#pragma once

// The heatmap's resampling and axis labels, shared by its cell and pixel
// versions (Charts.cpp, PixelCharts.cpp).

#include <cellui/HeatmapImage.h>
#include <cellui/Color.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cellui {

namespace heatmap_grid {

// Lerps from `background` (value == 0) to the fully-bright HSV color
// (value == 1), using value itself as the blend weight - continuous by
// construction, so a cell whose value only ever asymptotically approaches
// 0 (dsp/DiracAnalyzer.cpp's grid ballistics are a one-pole decay that
// never mathematically reaches exact 0) still converges to the background
// color rather than needing a separate cutoff/threshold to special-case
// "close enough to silent." No explicit epsilon check needed anywhere:
// once value is astronomically small, its weight in the lerp is too.
// `background` is window_bg_color, the idle background every scope
// shares - the heatmap repaints every cell/pixel each frame, so it has no
// untouched cells to show it through the way the other charts do.
inline FloatRgb heatmapCellColor(const Color & background, float saturation, float value) {
  FloatRgb c{};
  heatmap_image::cellColor(background, saturation, value, c.r, c.g, c.b);
  return c;
}

// Computes destination index d's source-index range [lo,hi) along one axis
// of size grid_size, resampled to dest_size - two different, deliberately
// separate strategies depending on direction, because a single formula
// that's correct for one gets the other wrong:
//
// - Downsampling (dest_size <= grid_size, TerminalHeatmapChart's usual
//   case): a proper partition via round(d*grid_size/dest_size) boundaries,
//   not floor or ceiling. lo(d) is literally hi(d-1), so consecutive
//   buckets never leave a gap (a truncating/floor upper bound can drop a
//   source cell a bucket's real span only partially reaches into - a real,
//   confirmed bug: grid_size=18, dest_size=15, d=7's real span [8.4,9.6)
//   genuinely overlaps both source rows 8 and 9, but floor(9.6)=9 as an
//   exclusive bound only ever included row 8) nor overlap (a ceiling upper
//   bound independently applied to every d, tried and reverted here, fixed
//   the gap but then had d=6's range reach into row 8 too, so an
//   elevation=0 source split 50/50 across rows 8/9 by DiracAnalyzer.cpp's
//   own bilinear splat lit up all 3 sextant sub-rows instead of
//   concentrating in the true middle one).
//
// - Upsampling (dest_size > grid_size, TerminalPixelHeatmapChart's usual
//   case, and TerminalHeatmapChart's own azimuth axis before
//   DiracAnalyzer::kAzimuthBins was widened enough to avoid it): the same
//   round-boundary formula can tie at consecutive d (two different d's
//   round to the same source index), leaving one of them an empty range;
//   patching that by grabbing the *next* source index (as the downsampling
//   fallback below does) breaks the tie asymmetrically, double-covering
//   one source cell while its symmetric neighbor only gets single
//   coverage - the horizontal counterpart of the elevation bug above (a
//   source split 50/50 across two adjacent azimuth bins rendered as three
//   equally-bright sub-columns instead of a symmetric two-and-two split).
//   Each destination index instead independently gets its own single
//   nearest source cell, by that destination cell's real-valued *center*
//   (d+0.5) rather than its edge - ties are then impossible since
//   consecutive d's centers can never round to the same source index the
//   way consecutive *boundaries* can.
inline void axisRange(int d, int grid_size, int dest_size, int & lo, int & hi) {
  if (dest_size <= grid_size) {
    lo = (d * grid_size + dest_size / 2) / dest_size;
    hi = ((d + 1) * grid_size + dest_size / 2) / dest_size;
    if (hi <= lo) hi = lo + 1; // degenerate (dest_size far smaller than grid_size): still show something
  } else {
    lo = ((2 * d + 1) * grid_size) / (2 * dest_size); // floor((d+0.5)*grid_size/dest_size)
    hi = lo + 1;
  }
  if (lo >= grid_size) lo = grid_size - 1;
  if (hi > grid_size) hi = grid_size;
}

// Brightness and brightness-weighted saturation: interpolating the weighted
// form keeps a dark cell's saturation from tinting its lit neighbour.
struct BrightSat { float b = 0.0f, sb = 0.0f; };

// Resamples one line of `src_n` cells to `dst_n`: smoothly (linear between
// cell centres) when enlarging, and by axisRange()'s max when shrinking, so a
// narrow peak survives.
inline void resampleLine(const BrightSat * in, int in_stride, int src_n, BrightSat * out, int out_stride, int dst_n) {
  for (int d = 0; d < dst_n; d++) {
    BrightSat result;
    if (dst_n > src_n) {
      float pos = (static_cast<float>(d) + 0.5f) * static_cast<float>(src_n) / static_cast<float>(dst_n) - 0.5f;
      int i0 = static_cast<int>(std::floor(pos));
      float t = pos - static_cast<float>(i0);
      const BrightSat & a = in[std::clamp(i0, 0, src_n - 1) * in_stride];
      const BrightSat & b = in[std::clamp(i0 + 1, 0, src_n - 1) * in_stride];
      result = { a.b + (b.b - a.b) * t, a.sb + (b.sb - a.sb) * t };
    } else {
      int lo, hi;
      axisRange(d, src_n, dst_n, lo, hi);
      for (int i = lo; i < hi; i++) if (in[i * in_stride].b > result.b) result = in[i * in_stride];
    }
    out[d * out_stride] = result;
  }
}

// Resamples the logical grid_cols x grid_rows brightness/saturation grid to a
// dest_cols x dest_rows destination resolution (a coarse grid is smoothed up
// to real pixels or sub-cells, a fine one reduced by max), one axis at a time.
// Both dest_row 0 and grid row 0 are "bottom" here - callers flip to screen
// coordinates (row 0 = top) themselves.
inline void resampleGrid(const std::vector<float> & brightness, const std::vector<float> & saturation,
                   int grid_cols, int grid_rows, int dest_cols, int dest_rows,
                   std::vector<float> & out_brightness, std::vector<float> & out_saturation) {
  out_brightness.assign(static_cast<size_t>(dest_cols) * static_cast<size_t>(dest_rows), 0.0f);
  out_saturation.assign(static_cast<size_t>(dest_cols) * static_cast<size_t>(dest_rows), 0.0f);
  if (dest_cols <= 0 || dest_rows <= 0) return;

  std::vector<BrightSat> source(static_cast<size_t>(grid_cols) * static_cast<size_t>(grid_rows));
  for (size_t i = 0; i < source.size(); i++) source[i] = { brightness[i], brightness[i] * saturation[i] };

  // Columns first (each grid row to dest_cols), then rows (each dest column to dest_rows).
  std::vector<BrightSat> wide(static_cast<size_t>(dest_cols) * static_cast<size_t>(grid_rows));
  for (int gy = 0; gy < grid_rows; gy++) {
    resampleLine(&source[static_cast<size_t>(gy * grid_cols)], 1, grid_cols, &wide[static_cast<size_t>(gy * dest_cols)], 1, dest_cols);
  }
  std::vector<BrightSat> result(static_cast<size_t>(dest_cols) * static_cast<size_t>(dest_rows));
  for (int dx = 0; dx < dest_cols; dx++) {
    resampleLine(&wide[static_cast<size_t>(dx)], dest_cols, grid_rows, &result[static_cast<size_t>(dx)], dest_cols, dest_rows);
  }
  for (size_t i = 0; i < result.size(); i++) {
    out_brightness[i] = std::clamp(result[i].b, 0.0f, 1.0f);
    out_saturation[i] = result[i].b > 1e-9f ? std::clamp(result[i].sb / result[i].b, 0.0f, 1.0f) : 0.0f;
  }
}

struct AxisLabel { int row, col; const char * text; };

// The heatmap's own axis extremes, placed directly in the grid area rather
// than a separate legend row - elevation labels on the top/bottom rows
// (matching HeatmapChart's row-0-is-bottom-of-data/screen-row-0-is-top
// convention, so "+90" naturally sits where the top of the grid is),
// azimuth labels at the left/right edges of a middle row (azimuth bin 0
// is the left edge, bin kAzimuthBins-1 the right - see DiracAnalyzer.cpp's
// own az_pos splat math). Labels are always drawn, never hidden by
// activity underneath them - instead, each character cell a label
// occupies is an exception to the normal two-color quadrant/pixel
// blend: it collapses to a single mean color (its underlying sub-samples
// averaged) used as that character's background, with the glyph itself
// drawn in white blended with transparency over that background
// (labelForegroundColor(), below) - so the label always reads clearly
// while still visibly taking on whatever hue/brightness is really there.
constexpr float kLabelForegroundAlpha = 0.55f; // how much white shows through the glyph

inline void labelForegroundColor(const FloatRgb & bg, uint8_t & r, uint8_t & g, uint8_t & b) {
  r = static_cast<uint8_t>(kLabelForegroundAlpha * 255.0f + (1.0f - kLabelForegroundAlpha) * bg.r);
  g = static_cast<uint8_t>(kLabelForegroundAlpha * 255.0f + (1.0f - kLabelForegroundAlpha) * bg.g);
  b = static_cast<uint8_t>(kLabelForegroundAlpha * 255.0f + (1.0f - kLabelForegroundAlpha) * bg.b);
}

inline std::array<AxisLabel, 4> axisLabels(int usable_rows, int cols) {
  int el_col = std::max(0, (cols - 3) / 2); // "+90"/"-90" are both 3 characters wide
  return {{
    { 0, el_col, "+90" },
    { usable_rows - 1, el_col, "-90" },
    { usable_rows / 2, 0, "-180" },
    { usable_rows / 2, cols - 4, "+180" },
  }};
}


}  // namespace heatmap_grid

}  // namespace cellui
