#ifndef _MIXER_H_
#define _MIXER_H_

#include "SampleData.h"

class Mixer {
 public:
  Mixer(short out_channels, int outSampleRate) : out_channels_(out_channels), outSampleRate_(outSampleRate) { }
  virtual ~Mixer() { }
  
  virtual void reset() = 0;
  virtual void accumulate(const SampleData & data, float volume = 1.0f) = 0;
  virtual SampleData encode(float master_volume) = 0;

  short getOutChannels() const { return out_channels_; }
  int getOutSampleRate() const { return outSampleRate_; }
  
private:
  short out_channels_;
  int outSampleRate_;
};

#endif
