#ifndef _MIXER_H_
#define _MIXER_H_

#include "../audio/AudioBuffer.h"

class Mixer {
 public:
  Mixer(short out_channels, int outSampleRate) : out_channels_(out_channels), outSampleRate_(outSampleRate) { }
  virtual ~Mixer() { }

  // Clears the accumulator for the next block. A mixer that accumulated
  // nothing this block (see accumulated()) has nothing to clear.
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
  // meter to show levels before mixdown. When nothing was accumulated this
  // block it is structurally empty - no regular channels, no aux, but still
  // frame-sized - so hasChannel(Channel::Main) is the test for silence.
  virtual const AudioBuffer & getRawBus() const = 0;

  short getOutChannels() const { return out_channels_; }
  int getOutSampleRate() const { return outSampleRate_; }

 protected:
  // True once a block's accumulate() calls have added at least one regular
  // channel, until the next reset(). While false the accumulator is all
  // zeros by construction, so reset() and the decode have nothing to do.
  bool accumulated() const { return accumulated_; }

  // Called by an implementation's accumulate() once its own accumulator has
  // the input's frame count: keeps the empty stand-in for getRawBus() the
  // same size and notes whether `input` carried anything to add.
  void noteAccumulated(const AudioBuffer & input) {
    if (empty_bus_.numberOfFrames() != input.numberOfFrames()) {
      empty_bus_ = AudioBuffer(0, false, false, input.numberOfFrames());
    }
    if (input.regularChannelCount() > 0) accumulated_ = true;
  }

  // What getRawBus() returns: the accumulator itself once something was
  // added, else the structurally empty buffer.
  const AudioBuffer & rawBusOf(const AudioBuffer & accumulator) const {
    return accumulated_ ? accumulator : empty_bus_;
  }

  // Zeroes `accumulator` if (and only if) it holds anything, and starts a
  // new block.
  void resetAccumulator(AudioBuffer & accumulator) {
    if (accumulated_) accumulator.zero();
    accumulated_ = false;
  }

private:
 bool accumulated_ = false;
 AudioBuffer empty_bus_;
 short out_channels_;
 int outSampleRate_;
};

#endif
