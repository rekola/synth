#ifndef _HEATMAPCHART_H_
#define _HEATMAPCHART_H_

#include "UIElement.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// 2D-grid visualization contract - deliberately separate from Chart's 1D
// setSample()/displayFFT() contract (Chart.h): this widget has no notion
// of "bars", just a rectangular grid of colored cells plus a handful of
// point markers overlaid on it. Currently backs the DirAC directional
// heatmap (plans/dirac-heatmap-scope.md) but not named after it, in case a
// future 2D visualization reuses this base.
class HeatmapChart : public UIElement {
 public:
  // Normalized [0,1) position within the logical grid - u = column axis
  // (e.g. azimuth), v = row axis (e.g. elevation), (0,0) = bottom-left,
  // matching setGrid()'s own row-0-is-bottom convention below.
  struct Marker { float u, v; };

  explicit HeatmapChart(UIPlane & plane, int grid_cols, int grid_rows)
    : UIElement(plane), grid_cols_(grid_cols), grid_rows_(grid_rows) { }

  // `brightness`/`saturation` are each gridCols()*gridRows() entries,
  // row-major with row 0 = bottom (matching DiracAnalyzer::getGrid()'s own
  // el_bin*kAzimuthBins+az_bin indexing, el_bin 0 = the lowest elevation
  // band), both already fully computed by the caller and clamped to [0,1]
  // - hue is fixed, owned by the renderer, not passed in (see
  // plans/dirac-heatmap-scope.md SS6).
  virtual void setGrid(const std::vector<float> & brightness, const std::vector<float> & saturation) = 0;
  virtual void setMarkers(std::vector<Marker> markers) = 0;
  virtual void commit() { }

  // Same contract as Chart::setFooterLabel() - a single string on this
  // widget's own bottom row.
  void setFooterLabel(std::string label) { footer_label_ = std::move(label); }

  int gridCols() const { return grid_cols_; }
  int gridRows() const { return grid_rows_; }

 protected:
  std::string footer_label_;

 private:
  int grid_cols_, grid_rows_;
};

// Fixed hue every HeatmapChart renderer paints with - the hue of
// StyleProvider.h's command_column_color ("#c67610", RGB 198/118/16),
// derived once here (60*((118-16)/182) = 33.63) rather than duplicated as
// an independently-rounded constant in each renderer, per
// plans/dirac-heatmap-scope.md SS6 ("fixed hue... for UI-wide consistency").
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

#endif
