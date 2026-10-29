#pragma once

#include <cellui/Widget.h>

#include <vector>

namespace cellui {

// The scope row's live spectrum: log-frequency bars. How they are drawn is
// the display's (Scopes::spectrum()). No peak hold.
class SpectrumMeter : public Widget {
public:
  explicit SpectrumMeter(Plane & parent) : Widget(parent) { }
  ~SpectrumMeter() override = default;

  // A fresh spectrum, dBFS per linear frequency bin `bin_hz` apart from DC.
  // Redraws, unless the curve is where it was drawn; returns whether it did.
  bool setSpectrum(const std::vector<float> & db, float bin_hz);

  // Shows no bars at all. Redraws.
  void clear();

 protected:
  // How many bars the display shows across `cols` cells.
  virtual size_t barCount(int cols) = 0;

  // Draws the bars, each a 0..1 level (same size as barCount()). Clears and
  // redraws the whole plane.
  virtual void drawBars(const std::vector<float> & levels) = 0;

  static std::vector<float> splineSample(const std::vector<float> & knots, size_t count);

private:
  // The dBFS span shown, 0 dB at the top; the axis is log-frequency from
  // kMinHz up to Nyquist.
  static constexpr float kRangeDb = 80.0f;
  static constexpr float kMinHz = 40.0f;

  // What was last drawn: the spline's knots (to 1/512), and the size.
  std::vector<int> last_knots_;
  int last_rows_ = 0, last_cols_ = 0;
  size_t last_bars_ = 0;

};

}  // namespace cellui
