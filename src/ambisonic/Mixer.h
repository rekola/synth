#ifndef _MIXER_H_
#define _MIXER_H_

#include "../audio/AudioBuffer.h"

class Mixer {
 public:
  Mixer(short out_channels, int outSampleRate) : out_channels_(out_channels), outSampleRate_(outSampleRate) { }
  virtual ~Mixer() { }
  
  virtual void reset() = 0;
  // `data` may carry AuxA/AuxB (see AudioBuffer.h's Channel enum) - a
  // track's rendered output can have them correctly summed within its own
  // hierarchy (see TrackState::renderChildren/InstrumentTrackState::render).
  // The mixer itself never stores or acts on them: implementations
  // accumulate via AudioBuffer::mixNamed(), which only ever touches
  // channels the mixer's own accumulator has itself marked present (never
  // AuxA/AuxB), so any aux channels on `data` are silently ignored here -
  // not because nothing consumes them, but because SongState::renderBlock()
  // extracts and sums them separately (getChannel(Channel::AuxA/AuxB),
  // right after this accumulate() call) to feed the shared reverb/chorus
  // bus (bus/SendBusProcessor.h) directly, bypassing the mixer entirely.
  virtual void accumulate(const AudioBuffer & data) = 0;
  virtual AudioBuffer encode() = 0;

  // The raw, pre-decode accumulator - regular (Main) channels only, for
  // whatever ChannelConfiguration this mixer was built for (1 for MONO,
  // 4/9/16 for AMBISONIC orders 1-3). Used by the UI's raw-channel volume
  // meter to show levels before mixdown.
  virtual const AudioBuffer & getRawBus() const = 0;

  short getOutChannels() const { return out_channels_; }
  int getOutSampleRate() const { return outSampleRate_; }
  
private:
  short out_channels_;
  int outSampleRate_;
};

#endif
