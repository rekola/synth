#pragma once

#include <cellui/Widget.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace cellui {

// 2D-grid visualization contract - deliberately separate from Chart's 1D
// setSample()/displayFFT() contract (the 1D chart): this widget has no notion
// of "bars", just a rectangular grid of colored cells plus a handful of
// point markers overlaid on it. Currently backs the DirAC directional
// heatmap but not named after it, in case a future 2D visualization reuses
// this base.
class HeatmapChart : public Widget {
 public:
  // Normalized [0,1) position within the logical grid - u = column axis
  // (e.g. azimuth), v = row axis (e.g. elevation), (0,0) = bottom-left,
  // matching setGrid()'s own row-0-is-bottom convention below.
  struct Marker { float u, v; };

  // A text label the owner wants over the grid. (u, v) is a point in the same
  // normalized coordinates as Marker; (align_x, align_y) says which point of
  // the text sits on it - 0 is the left / bottom edge, 0.5 the middle, 1 the
  // right / top edge. So {0.5, 1, "+90", 0.5, 1} hangs "+90" centred from the
  // top edge, and {1, 0.5, "+180", 1, 0.5} ends it on the right edge, centred
  // vertically. A label that would leave the grid is moved back inside it.
  struct Label {
    float u, v;
    std::string text;
    float align_x = 0.5f, align_y = 0.5f;
  };

  explicit HeatmapChart(Plane & plane, int grid_cols, int grid_rows)
    : Widget(plane), grid_cols_(grid_cols), grid_rows_(grid_rows) { }

  // `brightness`/`saturation` are each gridCols()*gridRows() entries,
  // row-major with row 0 = bottom (matching DiracAnalyzer::getGrid()'s own
  // el_bin*kAzimuthBins+az_bin indexing, el_bin 0 = the lowest elevation
  // band), both already fully computed by the caller and clamped to [0,1]
  // - hue is fixed, owned by the renderer, not passed in.
  virtual void setGrid(const std::vector<float> & brightness, const std::vector<float> & saturation) = 0;
  virtual void setMarkers(std::vector<Marker> markers) = 0;
  // What is written over the grid, if anything: the owner decides, the
  // renderer only draws it (kept until set again).
  virtual void setLabels(std::vector<Label> labels) = 0;
  virtual void commit() { }

  int gridCols() const { return grid_cols_; }
  int gridRows() const { return grid_rows_; }

 private:
  int grid_cols_, grid_rows_;
};

// Fixed hue every HeatmapChart renderer paints with - the hue of the
// original theme's orange ("#c67610", RGB 198/118/16),
// derived once here (60*((118-16)/182) = 33.63) rather than duplicated as
// an independently-rounded constant in each renderer, for UI-wide consistency.
constexpr float kHeatmapHue = 33.63f;

// Standard HSV -> RGB (h in [0,360), s and v in [0,1]) - shared by every
// HeatmapChart renderer so the hue/gamma math can't independently drift
// between them.
inline void heatmapHsvToRgb(float h, float s, float v, uint8_t & r, uint8_t & g, uint8_t & b) {
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float rp = 0.0f, gp = 0.0f, bp = 0.0f;
  if (h < 60.0f)       { rp = c; gp = x; bp = 0.0f; }
  else if (h < 120.0f) { rp = x; gp = c; bp = 0.0f; }
  else if (h < 180.0f) { rp = 0.0f; gp = c; bp = x; }
  else if (h < 240.0f) { rp = 0.0f; gp = x; bp = c; }
  else if (h < 300.0f) { rp = x; gp = 0.0f; bp = c; }
  else                 { rp = c; gp = 0.0f; bp = x; }
  r = static_cast<uint8_t>((rp + m) * 255.0f);
  g = static_cast<uint8_t>((gp + m) * 255.0f);
  b = static_cast<uint8_t>((bp + m) * 255.0f);
}

}  // namespace cellui
