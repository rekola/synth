#ifndef _MIXER_H_
#define _MIXER_H_

#include "State.h"
#include "SampleData.h"

class Mixer : public State {
 public:
  Mixer(short _out_channels, int _outSampleRate) : State(_outSampleRate), out_channels(_out_channels) { }

  virtual void reset() = 0;
  virtual void accumulate(const SampleData & data, float volume = 1.0f) = 0;
  virtual SampleData encode(float master_volume) = 0;

  short getOutChannels() const { return out_channels; }
  
private:
  short out_channels;
};

#endif
