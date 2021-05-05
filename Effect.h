#ifndef _EFFECT_H_
#define _EFFECT_H_

#include "TreeElement.h"

class SampleData;

class Effect : public TreeElement {
 public:
  Effect() { }
  
  virtual void apply(SampleData & input) = 0;
  
 private:
  
};

#endif
