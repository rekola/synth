#ifndef _AMBISONICSTACKENCODER_H_
#define _AMBISONICSTACKENCODER_H_

#include "AmbisonicEncoding.h"

#include <cstddef>
#include <vector>

// Encodes several mono signals, each from its own direction, into one
// AudioBuffer's regular channels in a single pass. Gains ramp linearly
// across the block exactly like AmbisonicVoiceEncoder's, one persistent
// previous-gains set per signal.
//
// The work is a (channels x signals) by (signals x samples) product, so it
// runs eight samples at a time and keeps every channel's running sum for
// those samples in registers across all the signals, writing each output
// once - instead of one read-modify-write of every channel per signal.
class AmbisonicStackEncoder {
 public:
  // `dry` holds one row per signal, `stride` floats apart (each row at
  // least `frames` long); `targets` has the gains (already scaled) for
  // each row. The signal count must stay the same from call to call.
  void encodeBlock(AudioBuffer & out, const float * dry, size_t stride, const std::vector<AmbisonicGains> & targets, int frames);

 private:
  std::vector<AmbisonicGains> prev_;
  std::vector<float> g0_, dg_; // per signal and channel: ramp start and per-sample step
};

#endif
