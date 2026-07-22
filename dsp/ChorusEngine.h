#ifndef _CHORUSENGINE_H_
#define _CHORUSENGINE_H_

#include "../SampleData.h"

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

  // In place. Only ever touches this SampleData's regular (non-send)
  // channels - SendA/SendB, if present, are left untouched (see
  // SampleData::sendCount()), same reasoning as the fix applied to
  // ResonantFilter/BiquadFilter/Delay: this engine's per-channel state is
  // sized once, at construction, from the reduced (always real) channel
  // count, which never includes sends.
  void process(SampleData & data);

 private:
  struct ChannelState {
    std::vector<float> buffer; // circular delay line
    std::vector<float> phase;  // one LFO phase per voice
    int write_pos = 0;
  };

  int sampleRate_;
  int voices_;
  float rateHz_;
  float centerDelayMs_;
  float depthMs_;
  float mix_ = 0.5f;
  std::vector<ChannelState> channels_;
};

#endif
