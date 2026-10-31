#pragma once

#include <cellui/Widget.h>

#include <vector>

namespace cellui {

// A curve over the plane's width, filled below it: a series of values
// 0..1 spaced evenly from left to right. What the values mean (a spectrum on
// a log-frequency axis, a history, a profile) is the application's; how the
// curve is drawn is the display's (Charts::lineChart()).
class LineChart : public Widget {
public:
  explicit LineChart(Plane & parent) : Widget(parent) { }
  ~LineChart() override = default;

  // A fresh curve through `values` (each clamped to 0..1). Redraws, unless
  // the curve is where it was drawn; returns whether it did.
  bool setValues(const std::vector<float> & values);

  // A number of values that gives the display a point for every half cell
  // across the plane. More are fine, and fewer are smoothed between.
  size_t suggestedPoints() const;

  // Shows no curve at all. Redraws.
  void clear();

 protected:
  // How many samples the display draws across `cols` cells.
  virtual size_t sampleCount(int cols) = 0;

  // Draws the curve, one 0..1 sample per column of the display (same size as
  // sampleCount()). Clears and redraws the whole plane.
  virtual void drawSamples(const std::vector<float> & samples) = 0;

  static std::vector<float> splineSample(const std::vector<float> & knots, size_t count);

private:
  // What was last drawn: the values (to 1/512), and the size.
  std::vector<int> last_values_;
  int last_rows_ = 0, last_cols_ = 0;
  size_t last_samples_ = 0;
};

}  // namespace cellui
