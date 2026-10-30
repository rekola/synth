#pragma once

#include <cellui/DisplayInfo.h>
#include <cellui/HeatmapChart.h>
#include <cellui/LineChart.h>
#include <cellui/SparklineImage.h>

#include <memory>
#include <vector>

namespace cellui {

// A waveform is drawn a pattern row at a time from this many time slices of
// the row.
constexpr int kWaveformSlices = 12;

// How a waveform looks: the bar's colour, what it is drawn on, and the
// neutral colour of its guide lines.
struct WaveformLook {
  Color fg;
  Color bg;
  Color guide;
};

// The charts - a line chart, a heatmap, sparklines and a waveform - in the versions a backend can show. An application asks its
// backend's Charts for a chart and never knows how it is drawn.
class Charts {
 public:
  virtual ~Charts() = default;

  virtual std::shared_ptr<LineChart> lineChart(Plane & parent) const = 0;
  // A heatmap of a grid_cols x grid_rows grid.
  virtual std::shared_ptr<HeatmapChart> heatmap(Plane & parent, int grid_cols, int grid_rows) const = 0;
  // A sparkline drawn into a widget's own plane: a bar `rows` cells tall from
  // (row, col), one cell wide, showing a value and a mark above it, drawn only
  // in the plane rows from clip_top up to clip_bottom (one partly scrolled out
  // of view). `id` names the sparkline for the plane's images (see
  // Plane::drawImageAt()); 0 for none.
  virtual void drawSparkline(Plane & plane, int row, int col, int rows, const SparklineReading & reading, const SparklineLook & look,
                             int clip_top, int clip_bottom, uint64_t id = 0) const = 0;
  // One pattern row's slice of a sample clip's waveform across `width` cells
  // from (row, col): kWaveformSlices amplitudes (0..1) over the row's time,
  // each a bar centred in the row's width.
  virtual void drawWaveformRow(Plane & plane, int row, int col, int width, const std::vector<float> & slices,
                               const WaveformLook & look) const = 0;
};

// The fallback for a display with no pictures, drawn in cells (cellui/term/
// CellCharts.h). Every backend can show these. `sextants`: the display can
// draw the finer glyphs (DisplayInfo::sextants).
std::unique_ptr<Charts> cellCharts(bool sextants = false);

// Drawn as pixels with canvas, for a backend whose planes show images
// (Plane::pixelSize()/drawImage()). Not available where canvas has no
// pixel backend built (nullptr).
std::unique_ptr<Charts> pixelCharts();

// The scopes a display shows: the pixel version when pictures can be shown,
// else the cell version.
inline std::unique_ptr<Charts> chartsFor(const DisplayInfo & display) {
  if (display.pictures && display.cell_px_height > 0 && display.cell_px_width > 0) {
    if (auto pixel = pixelCharts()) return pixel;
  }
  return cellCharts(display.sextants);
}

}  // namespace cellui
