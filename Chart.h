#ifndef _CHART_H_
#define _CHART_H_

#include "UIElement.h"

class SampleData;

class Chart : public UIElement {
 public:
  enum ChartType {
		  DOTS = 1,
		  BLOCKS,		  
  };
  explicit Chart(UIPlane & plane, ChartType _type) : UIElement(plane), type(_type) { }
  
  void displayFFT(const std::vector<float> & v) {
    auto [ rows, columns ] = getDim();
    if (columns > 0) {
      int num_bins = 2 * columns;      
      float start_value = log(40), end_value = log(40 + v.size());
      float bin_size = (end_value - start_value) / num_bins;
    
      std::vector<float> bins;
      for (int i = 0; i < num_bins; i++) bins.push_back(0);

      for (int i = 0; i < v.size(); i++) {
	size_t i2 = (size_t)((log(40 + i) - start_value) / bin_size);
	if (v[i] > bins[i2]) bins[i2] = v[i];
	// bins[i2] += mag;
      }
  
      for (size_t i = 0; i < bins.size(); i++) {
	setSample(i, bins[i]);
      }
    }
  }

  virtual void setSample(int i, double v) = 0;

  ChartType getType() const { return type; }
private:
  ChartType type;
};

#endif
