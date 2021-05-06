#ifndef _EFFECTSTATE_H_
#define _EFFECTSTATE_H_

#include "SampleData.h"

class EffectState {
 public:
  EffectState(unsigned int _samplerate) : samplerate(_samplerate) { }
  virtual ~EffectState() { }

  virtual void apply(SampleData & input_data) = 0;

private:
  unsigned int samplerate;
};

#endif
