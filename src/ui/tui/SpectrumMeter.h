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

  // A fresh spectrum, one magnitude in dB per linear frequency bin. Redraws.
  void setSpectrum(const std::vector<float> & db);

private:
  // The dB span from the loudest a full-scale signal can read down to silence.
  static constexpr float kRangeDb = 80.0f;

  std::vector<level_meter::PeakHold> peak_holds_;
  std::chrono::steady_clock::time_point last_update_;
};

#endif
