#ifndef _SPECTRUMMETER_H_
#define _SPECTRUMMETER_H_

#include "../UIElement.h"
#include "LevelMeter.h"

#include <chrono>
#include <vector>

// The scope row's live spectrum: log-frequency bars drawn with the same
// sub-cell renderer as the volume meters, two bins per cell.
class SpectrumMeter : public UIElement {
public:
  explicit SpectrumMeter(UIPlane & parent) : UIElement(parent) { }
  ~SpectrumMeter() override = default;

  // A fresh spectrum, dBFS per linear frequency bin `bin_hz` apart from DC.
  // Redraws.
  void setSpectrum(const std::vector<float> & db, float bin_hz);

protected:
  // How many bars the display shows across `cols` cells; braille packs two
  // per cell, a pixel renderer can use one per pixel column.
  virtual size_t barCount(int cols) { return static_cast<size_t>(2 * cols); }

  // Draws the bars, each a 0..1 level with a 0..1 peak-hold marker (same
  // size as barCount()). Clears and redraws the whole plane.
  virtual void drawBars(const std::vector<float> & levels, const std::vector<float> & peaks);

private:
  // The dBFS span shown, 0 dB at the top; the axis is log-frequency from
  // kMinHz up to Nyquist.
  static constexpr float kRangeDb = 80.0f;
  static constexpr float kMinHz = 40.0f;

  std::vector<level_meter::PeakHold> peak_holds_;
  std::chrono::steady_clock::time_point last_update_;
};

#endif
