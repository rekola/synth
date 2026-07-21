#ifndef _MIXER_H_
#define _MIXER_H_

#include "SampleData.h"

class Mixer {
 public:
  Mixer(short out_channels, int outSampleRate) : out_channels_(out_channels), outSampleRate_(outSampleRate) { }
  virtual ~Mixer() { }
  
  virtual void reset() = 0;
  // `data` may carry SendA/SendB (see SampleData.h's Channel enum) - a
  // track's rendered output can have them correctly summed within its own
  // hierarchy (see TrackState::renderChildren/InstrumentTrackState::render).
  // The mixer itself never stores or acts on them: implementations accumulate via
  // SampleData::mixNamed(), which only ever touches channels the mixer's
  // own accumulator has itself marked present (never SendA/SendB), so any
  // sends on `data` are silently ignored here - nothing consumes them yet;
  // Phase 2 adds the reverb/chorus that will, tapping tracks directly
  // rather than through the mixer.
  virtual void accumulate(const SampleData & data) = 0;
  virtual SampleData encode() = 0;

  // The raw, pre-decode accumulator - regular (non-send) channels only, for
  // whatever ChannelConfiguration this mixer was built for (1/2/4/9). Used
  // by the UI's raw-channel volume meter to show levels before mixdown.
  virtual const SampleData & getRawBus() const = 0;

  short getOutChannels() const { return out_channels_; }
  int getOutSampleRate() const { return outSampleRate_; }
  
private:
  short out_channels_;
  int outSampleRate_;
};

#endif
