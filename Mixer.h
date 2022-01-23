#ifndef _MIXER_H_
#define _MIXER_H_

#include "State.h"
#include "SampleData.h"

class Mixer : public State {
 public:
  Mixer(unsigned int _out_channels, unsigned int _outSampleRate) : State(_outSampleRate), out_channels(_out_channels) { }

  virtual void reset() = 0;
  virtual void accumulate(const SampleData & data, float volume = 1.0f) = 0;
  virtual SampleData encode(float master_volume) = 0;

  unsigned int getOutChannels() const { return out_channels; }
  
private:
  unsigned int out_channels;
};

#endif
