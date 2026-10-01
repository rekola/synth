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

  // A fresh spectrum, dBFS per linear frequency bin `bin_hz` apart from DC.
  // Redraws.
  void setSpectrum(const std::vector<float> & db, float bin_hz);

private:
  // The dBFS span shown, 0 dB at the top; the axis is log-frequency from
  // kMinHz up to Nyquist.
  static constexpr float kRangeDb = 80.0f;
  static constexpr float kMinHz = 40.0f;

  std::vector<level_meter::PeakHold> peak_holds_;
  std::chrono::steady_clock::time_point last_update_;
};

#endif
