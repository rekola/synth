#pragma once

#include <cellui/Charts.h>
#include <cellui/LineChart.h>

namespace cellui::term {

// The scopes drawn in cells by hand - braille bars, a quadrant or sextant
// heatmap, a quadrant or sextant waveform, the labels in text - for a display
// that shows no pictures. Nothing outside the terminal backend knows these
// glyphs: an application asks its display's Charts and gets whichever version
// it can show.

// Two bars to a cell.
class CellLineChart : public LineChart {
 public:
  using LineChart::LineChart;

 protected:
  size_t sampleCount(int cols) override { return static_cast<size_t>(2 * cols); }
  void drawSamples(const std::vector<float> & samples) override;
};

void drawCellSparkline(Plane & plane, int row, int col, int rows, const std::vector<SparklineReading> & readings, const SparklineLook & look, int clip_top,
                     int clip_bottom);

void drawCellWaveformRow(Plane & plane, int row, int col, int width, const std::vector<float> & slices, const WaveformLook & look,
                         bool sextants);

}  // namespace cellui::term
