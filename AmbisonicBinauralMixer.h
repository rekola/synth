#ifndef _AMBISONICBINAURALMIXER_H_
#define _AMBISONICBINAURALMIXER_H_

#include "Mixer.h"
#include "AmbisonicEncoding.h"

#include <vector>

struct MYSOFA_EASY;

// Binaural ambisonic decoder (only compiled when SYNTH_HAVE_LIBMYSOFA is
// defined - see CMakeLists.txt): decodes the ambisonic bus (up to 3rd
// order) to a fixed virtual speaker rig (8-point cube at order 1, 12-point
// icosahedron at order 2, 26-point Lebedev grid at order 3 - see
// speakerDirectionsFor() in the .cpp), weighted by max-rE per-degree gains
// (see AmbisonicEncoding.h's maxReGainsPerDegree), convolves each
// speaker/ear pair with an HRIR loaded via libmysofa (resampled to the
// engine's sample rate, per-ear onset delay preserved), sums to stereo.
// Falls back to AmbisonicStereoMixer (MixerFactory.cpp) if isReady() is
// false - no SOFA file resolved.
class AmbisonicBinauralMixer : public Mixer {
 public:
  AmbisonicBinauralMixer(int ambisonic_channels, int outSampleRate);
  ~AmbisonicBinauralMixer() override;

  bool isReady() const { return ready_; }

  void reset() override;
  void accumulate(const SampleData & input) override;
  SampleData encode() override;

  const SampleData & getRawBus() const override { return buffer_; }

 private:
  struct SpeakerFilter {
    AmbisonicGains decode_gains;
    std::vector<float> left_ir, right_ir;
    int left_delay = 0, right_delay = 0;
  };

  int ambisonic_channels_;
  bool ready_ = false;
  MYSOFA_EASY * easy_ = nullptr;
  std::vector<SpeakerFilter> speakers_;

  SampleData buffer_;

  // Overlap-add tail carried between encode() calls, so the convolution is
  // seamless across block boundaries. Sized once at construction from the
  // longest IR + largest delay across all speaker/ear filters.
  std::vector<float> left_tail_, right_tail_;

  // Scratch buffers reused across encode() calls - resize() only grows the
  // underlying allocation the first time a given (or larger) block size is
  // seen, so this doesn't allocate on the audio thread once warmed up
  // (unlike freshly constructing a std::vector per call, which always
  // allocates+zero-initializes).
  std::vector<float> left_acc_, right_acc_, speaker_signal_;

  // Headroom against summing this instance's own speaker count worth of
  // HRIR energy - NOT a single constant shared across every order, since
  // both things it depends on (how many speakers get summed, and the
  // max-rE weight applied to the W/ACN0 term - see maxReGainsPerDegree)
  // differ by order: order 1 sums only 8 (cube) speakers, order 2 sums 12
  // (icosahedron), order 3 sums 26 (Lebedev grid - see
  // speakerDirectionsFor()); a single trim calibrated for order 3's worst
  // case would leave order 1/2 needlessly quiet (order 2 alone would come
  // out ~2.3x quieter than its own correct calibration), which is exactly
  // the kind of non-algebraic, order-dependent loudness drift the rest of
  // this max-rE work is trying to avoid. So this is computed once per
  // instance, in the constructor, from this instance's own actual speaker
  // count and k*g0.
  //
  // History/derivation method (unchanged from before max-rE existed):
  // originally 0.35, halved to 0.175 alongside the kAmbisonicReferenceGain
  // fix (AmbisonicEncoding.h, 1/sqrt(2) -> 1.0) - worst case is a fully
  // diffuse/unpositioned voice (computeAmbisonicGains' distance<=0 branch,
  // common - most tracks never set a position - returns W-only, every
  // other channel exactly 0), where the per-speaker signal is *entirely*
  // that W term. 0.175, calibrated for order 2's 12 speakers with an
  // *unweighted* decode (W-degree gain exactly 1.0 per speaker), is this
  // class's one fixed reference point; every other case scales from it:
  // gain_trim_ = 0.175 * (12 speakers * 1.0) / (this instance's speaker
  // count * this instance's k*g0) - see the constructor for the actual
  // computation, which reuses the same order/k it already derives for the
  // decode-matrix weighting itself. Worked examples: order 1 (8 speakers,
  // k*g0~1.414214) -> ~0.185616 (louder than the 0.175 reference - fewer
  // speakers than the reference case, even with weighting); order 2 (12
  // speakers, k*g0~1.581139) -> ~0.110680; order 3 (26 speakers,
  // k*g0~1.668184) -> ~0.048417. As before, this is an analytically
  // derived bound, not a re-tuned-by-ear value - listen and adjust the
  // 0.175 reference constant (not per-order figures individually) if
  // levels still seem off.
  float gain_trim_;
};

#endif
