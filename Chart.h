#ifndef _CHART_H_
#define _CHART_H_

#include "UIElement.h"

class SampleData;

class Chart : public UIElement {
 public:
  explicit Chart(UIPlane & plane) : UIElement(plane) { }
  
  void displayFFT(const SampleData & data, size_t channel);

  virtual void setSample(int i, double v) = 0;
};

#endif
