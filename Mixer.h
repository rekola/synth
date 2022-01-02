#ifndef _MIXER_H_
#define _MIXER_H_

#include "State.h"
#include "SampleData.h"

class Mixer : public State {
 public:
  Mixer(unsigned int _outSampleRate) : State(_outSampleRate) { }

  virtual void reset() = 0;
  virtual void accumulate(const SampleData & data, float volume = 1.0f, float distance = 0.0f, float azimuth = 0.0f, float elevation = 0.0f) = 0;
  virtual SampleData encode(float master_volume) = 0;  
};

#endif
