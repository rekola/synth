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
  
  void displayFFT(const SampleData & data);

  virtual void setSample(int i, double v) = 0;

  ChartType getType() const { return type; }
private:
  ChartType type;
};

#endif
