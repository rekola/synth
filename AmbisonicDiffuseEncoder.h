#ifndef _AMBISONICDIFFUSEENCODER_H_
#define _AMBISONICDIFFUSEENCODER_H_

#include "AmbisonicEncoding.h"
#include "SampleData.h"

#include <array>
#include <cstdint>
#include <vector>

// Mono-to-diffuse ambisonic encoder: routes a mono signal through 16
// independent, mutually-decorrelated allpass chains - one per ACN channel
// (order 0-3) - rather than gain-panning it toward any single direction.
// See plans/drum-bus-saturator.md for the full derivation of why this is
// a genuinely different operation from computeAmbisonicGains()-based
// point-source encoding (AmbisonicEncoding.h): every chain here carries
// the *identical* spectrum (Schroeder allpasses are unity-magnitude at
// every frequency, the same topology FDNReverb's own input diffusers use
// - bus/FDNReverb.cpp) but a different phase, which is what makes the
// result read as "arriving from everywhere" instead of "a point source
// with some particular loudness in every direction."
//
// Order weighting (sqrt(2n+1) per ACN degree n) compensates for SN3D
// normalization so equal-RMS decorrelated streams decode to a
// direction-independent energy field - see AmbisonicEncoding.h's
// acnDegree(). `diffusion` (0-1) is a tapered per-degree fade, not a
// flat scale - degree 3 channels reach full level first (over
// diffusion 0-1/3), then degree 2 (1/3-2/3), then degree 1 (2/3-1), so
// reducing `diffusion` collapses the field inward smoothly (order 3
// drops out last) rather than uniformly quieting every degree at once.
// Degree 0 (W) is never scaled by `diffusion` - it's unconditionally
// always the decorrelated-W chain at full level, so diffusion=0 is
// "W-only, centered" (still routed through its own allpass chain, not a
// raw undecorrelated copy - it just has no higher-degree siblings to
// give it a direction).
class AmbisonicDiffuseEncoder {
 public:
  // instanceSalt: distinguishes this instance's delay-length draws from
  // every other instance's, so two instances (e.g. Haze, the bus
  // saturator, and a later reverb/granular-cloud port onto this same
  // class - see plans/drum-bus-saturator.md's consolidation note) don't
  // return correlated, partially self-cancelling-or-reinforcing output
  // for the same input. Implemented as a cyclic shift into a shared
  // prime-length table (see delayLengthTable() in the .cpp) rather than
  // a strict per-instance-disjoint range - the 5-20ms/typical-sample-rate
  // window doesn't contain enough distinct primes to hand every channel
  // x stage x instance combination its own never-reused value, but a
  // shift is enough that two instances' full 64-length chain sets never
  // coincide. Reserve a distinct salt per call site (0 = Haze, the bus
  // saturator; other values available for future callers).
  AmbisonicDiffuseEncoder(int sampleRate, uint32_t instanceSalt);

  // mono/frames: the already-processed signal to diffuse-encode.
  // diffusion: 0-1, see the class comment above. gain: applied uniformly
  // to every channel on top of per-channel decorrelation/order weighting
  // (typically the caller's own wet level). Adds into `out`'s regular
  // channels (does not zero first - same accumulate convention as
  // encodeMonoAsPoint()/encodeStereoAsPoints(), AmbisonicEncoding.h).
  // Writes only as many of the 16 channels as out.regularChannelCount()
  // actually has (order 1 = 4, order 2 = 9, order 3 = 16) - same
  // graceful-degradation rule as AmbisonicVoiceEncoder::encodeBlock().
  void encode(SampleData & out, const float * mono, int frames, float diffusion, float gain);

 private:
  // Standard Schroeder allpass - same buffer/pos/gain shape and process
  // formula as FDNReverb's own diffusers_ (bus/FDNReverb.cpp), reused
  // here as a private inner type rather than factored out (it's an 8-line
  // struct, not worth sharing across translation units for this alone).
  struct AllpassStage {
    std::vector<float> buffer;
    int pos = 0;
    int length = 0;
  };

  static constexpr int kStagesPerChannel = 4;
  static constexpr float kAllpassGain = 0.6f;

  struct Chain {
    std::array<AllpassStage, kStagesPerChannel> stages;
    float processSample(float x);
  };

  std::array<Chain, kAmbisonicChannelCount> chains_;
  std::vector<float> scratch_; // one channel's worth of chain output, reused per channel per encode() call
};

#endif
