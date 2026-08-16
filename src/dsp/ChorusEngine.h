#ifndef _CHORUSENGINE_H_
#define _CHORUSENGINE_H_

#include "../audio/AudioBuffer.h"
#include "FractionalDelayLine.h"

#include <array>
#include <vector>

// Multi-voice, LFO-modulated, linearly-interpolated delay-line chorus.
// Never mixes across channels - each channel's wet signal is derived only
// from that channel's own delayed content, never another channel's. This
// is what lets the same engine correctly serve two different jobs:
//
//  - Fed an already-positioned signal (decorrelate = false, the per-track
//    Chorus effect's use): a channel that's silent (e.g. the left channel
//    of a hard-panned-right source) stays silent after chorusing - there's
//    nothing to leak in from elsewhere. Preserves whatever real stereo
//    image the input already had.
//  - Fed a mono signal duplicated into 2 identical channels (decorrelate =
//    true, the shared send-bus's use - see SendBusProcessor): channel 1's
//    voices get their LFO phases offset from channel 0's, so two initially
//    identical channels diverge into genuine stereo width. This only makes
//    sense because there's no pre-existing position to erase in that case
//    (a mono send has none).
class ChorusEngine {
 public:
  explicit ChorusEngine(int channels, int sampleRate, int voices = 3, float rateHz = 0.5f,
			float centerDelayMs = 15.0f, float depthMs = 4.0f, bool decorrelate = false);

  void setMix(float mix) { mix_ = mix; }

  // The furthest back (in samples) content already written into any
  // channel's delay line could still be waiting to be read out - the LFO
  // swings the actual read delay between centerDelayMs-depthMs and
  // centerDelayMs+depthMs, so this is the upper end of that range. Used
  // by a voice-attached caller (effects/Chorus.cpp's ChorusVoiceState) to
  // know how many more (silent-input) samples it must keep calling
  // process() for after its own input goes silent before it's safe to
  // reclaim the voice - see dsp/DelayLineTail.h.
  int getMaxDelaySamples() const {
    int delaySamples = static_cast<int>(centerDelayMs_ * 0.001f * static_cast<float>(sampleRate_));
    int depthSamples = static_cast<int>(depthMs_ * 0.001f * static_cast<float>(sampleRate_)) + 1;
    return delaySamples + depthSamples;
  }

  // In place. `channels_` (sized at construction) holds Main/regular
  // channel state, processed up to min(data.regularChannelCount(),
  // channels_.size()) - same reasoning as BiquadFilter/ResonantFilter.
  // AuxA/AuxB (see AudioBuffer.h) get their own always-present, dedicated
  // state (aux_channels_) instead of being folded into channels_ by
  // widening it: a caller like the shared send bus's ChorusBusEffect
  // passes exactly 2 real Main channels with no Aux concept at all, so
  // "the last two channels_ slots" can't universally mean Aux across every
  // caller - a separate, always-inert-unless-used pair sidesteps that.
  // Whichever of Main/AuxA/AuxB is momentarily absent (but has been
  // present before) still gets its delay line/LFO phases advanced through
  // silence rather than frozen, so it resumes correctly instead of as if
  // no time had passed - never touched at all until real data actually
  // arrives once.
  void process(AudioBuffer & data);

 private:
  struct ChannelState {
    FractionalDelayLine delay_line;
    std::vector<float> phase;  // one LFO phase per voice
  };

  static void initChannel(ChannelState & ch, int bufLen, int voices, float channel_offset);
  void processChannel(ChannelState & ch, float * buf, int frames, float delaySamples, float depthSamples, float dphi);
  void processSilence(ChannelState & ch, int frames, float dphi);

  int sampleRate_;
  int voices_;
  float rateHz_;
  float centerDelayMs_;
  float depthMs_;
  float mix_ = 0.5f;
  std::vector<ChannelState> channels_;
  std::array<ChannelState, 2> aux_channels_;
  bool main_ever_present_ = false;
  std::array<bool, 2> aux_ever_present_ { false, false };
};

#endif
