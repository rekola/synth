#ifndef _EFFECT_H_
#define _EFFECT_H_

#include "TreeElement.h"
#include "EffectState.h"

#include <memory>

class Effect : public TreeElement {
 public:
  Effect() { }
  
  virtual std::unique_ptr<EffectState> createState(unsigned int outSamplerate) const = 0;
};

#endif
