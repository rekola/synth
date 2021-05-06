#ifndef _EFFECT_H_
#define _EFFECT_H_

#include "TreeElement.h"
#include "EffectState.h"

#include <memory>

class SampleData;

class Effect : public TreeElement {
 public:
  Effect() { }
  
  virtual void apply(SampleData & input) = 0;
  virtual std::unique_ptr<EffectState> createState(unsigned int samplerate) { return std::unique_ptr<EffectState>(nullptr); }

 private:
  
};

#endif
