#ifndef _EFFECTSTATE_H_
#define _EFFECTSTATE_H_

#include "State.h"
#include "TrackInfo.h"

class SampleData;

class EffectState : public State {
 public:
  EffectState(unsigned int _samplerate) : State(_samplerate) { }

  virtual void apply(SampleData & input_data) = 0;
  virtual TrackInfo getInfo() const { return TrackInfo(true); }
};

#endif
