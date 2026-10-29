#pragma once

#include <cellui/ChannelMeter.h>
#include <cellui/Scopes.h>
#include <cellui/SpectrumMeter.h>

namespace cellui::term {

// The scopes drawn in cells by hand - braille bars, a quadrant or sextant
// heatmap, a quadrant or sextant waveform, the labels in text - for a display
// that shows no pictures. Nothing outside the terminal backend knows these
// glyphs: an application asks its display's Scopes and gets whichever version
// it can show.

// Two bars to a cell.
class CellSpectrumMeter : public SpectrumMeter {
 public:
  using SpectrumMeter::SpectrumMeter;

 protected:
  size_t barCount(int cols) override { return static_cast<size_t>(2 * cols); }
  void drawBars(const std::vector<float> & levels) override;
};

// Two channels to a cell.
class CellChannelMeter : public ChannelMeter {
 public:
  using ChannelMeter::ChannelMeter;

 protected:
  void drawBars(int bar_rows, int cells) override;
};

void drawCellVuMeter(Plane & plane, int row, int col, int rows, const VuReading & reading, const VuMeterLook & look, int clip_top,
                     int clip_bottom);

void drawCellWaveformRow(Plane & plane, int row, int col, int width, const std::vector<float> & slices, const WaveformLook & look,
                         bool sextants);

}  // namespace cellui::term
