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
  // HRIR energy - computed once per instance, in the constructor, entirely
  // from things measured or derived at load time, not a single constant
  // shared across every order/dataset. Three things determine the worst
  // case (a fully diffuse/unpositioned voice - computeAmbisonicGains'
  // distance<=0 branch, common, since most tracks never set a position -
  // where the per-speaker signal is *entirely* the W term):
  //   - speaker count: 8 (cube, order 1), 12 (icosahedron, order 2), 26
  //     (Lebedev grid, order 3 - see speakerDirectionsFor()). A trim
  //     calibrated for order 3's speaker count would leave order 1/2
  //     needlessly quiet if applied uniformly.
  //   - k*g0: max-rE's renormalized weight on the W/ACN0 term (see
  //     maxReGainsPerDegree) - also order-dependent.
  //   - dataset_filter_energy: the actual measured L2-norm ("energy") of a
  //     typical filter in whichever HRTF dataset is loaded - sqrt(mean over
  //     every (speaker,ear) filter of the sum of its own squared taps),
  //     accumulated in the constructor while every speaker's left_ir/
  //     right_ir are fetched. Deliberately NOT plain RMS-per-tap: a
  //     filter's contribution to convolution output power scales with its
  //     total energy (RMS * sqrt(filter length)), not RMS alone, so two
  //     datasets can differ heavily in loudness contribution even at
  //     similar per-tap RMS if their filter lengths differ (confirmed: a
  //     real KU100 compilation's filters measure both louder per-tap AND
  //     much shorter than an earlier-installed set - normalizing by RMS
  //     alone got the correction direction right but undercorrected badly).
  //     Different datasets can't share a fixed constant here, on pain of
  //     clipping or being needlessly quiet depending on which way a future
  //     dataset swap goes (confirmed - clipping is what happened before
  //     this term existed at all).
  //
  // gain_trim_ = kGainTrimTarget / (speaker_count * k*g0 *
  // dataset_filter_energy) (AmbisonicBinauralMixer.cpp - see the
  // constructor for the actual computation). kGainTrimTarget is the one
  // remaining tunable, empirically calibrated (not derived - "how loud the
  // worst case should be" is a product choice) so real program material
  // renders at a consistent, non-clipping peak/RMS across all three orders
  // regardless of which dataset is loaded. Listen and adjust that one
  // constant (AmbisonicBinauralMixer.cpp) if levels still seem off on a
  // future dataset swap.
  float gain_trim_;
};

#endif
