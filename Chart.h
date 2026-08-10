#ifndef _CHART_H_
#define _CHART_H_

#include "UIElement.h"

#include <string>
#include <utility>

class AudioBuffer;

class Chart : public UIElement {
 public:
  enum ChartType {
		  DOTS = 1,
		  BLOCKS,		  
  };
  explicit Chart(UIPlane & plane, ChartType type, double min_y = 0.0, double max_y = 0.0)
    : UIElement(plane), type_(type), min_y_(min_y), max_y_(max_y) { }
  
  void displayFFT(const std::vector<float> & v) {
    auto [ rows, columns ] = getDim();
    if (columns > 0) {
      int num_bins = 2 * columns;      
      float start_value = log2(40), end_value = log2(40 + v.size());
      float bin_size = (end_value - start_value) / num_bins;
    
      std::vector<float> bins;
      for (int i = 0; i < num_bins; i++) bins.push_back(0);

      for (int i = 0; i < v.size(); i++) {
	size_t i2 = (size_t)((log2(40 + i) - start_value) / bin_size);
	if (v[i] > bins[i2]) bins[i2] = v[i];
	// bins[i2] += mag;
      }
  
      for (size_t i = 0; i < bins.size(); i++) {
	setSample(i, bins[i]);
      }
    }

    commit();
  }

  virtual void setSample(int i, double v) = 0;

  // Called once after a full update (displayFFT()'s setSample() loop, or
  // directly by callers driving setSample() themselves, e.g. a volume
  // meter). No-op for renderers that draw immediately per-sample (ncplot);
  // renderers that batch a full-buffer redraw (pixel/sixel) override this
  // to do the actual expensive build-and-blit work exactly once per update
  // instead of once per sample.
  virtual void commit() { }

  // A single legend string (e.g. "A0-A8 S") shown on this chart's own
  // bottom row, not per-sample-aligned - callers with few columns to work
  // with (e.g. a narrow multi-bar meter) can't fit one aligned label per
  // bar anyway. Both renderers reserve exactly one row of their own height
  // for it once set, rather than the label being overdrawn by/over the
  // sample graphics - see TerminalChart::setSample()/TerminalPixelChart::commit().
  void setFooterLabel(std::string label) { footer_label_ = std::move(label); }

  ChartType getType() const { return type_; }

protected:
  double min_y_, max_y_;
  std::string footer_label_;

private:
  ChartType type_;
};

#endif
