#ifndef _AMBISONICBINAURALMIXER_H_
#define _AMBISONICBINAURALMIXER_H_

#include "Mixer.h"
#include "AmbisonicEncoding.h"

#include <vector>

struct MYSOFA_EASY;

// Binaural ambisonic decoder (only compiled when SYNTH_HAVE_LIBMYSOFA is
// defined - see CMakeLists.txt): decodes the ambisonic bus (up to 2nd
// order) to a fixed 12-speaker virtual icosahedron, convolves each
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

  // Headroom against summing 12 speakers' worth of HRIR energy. Halved
  // (was 0.35) alongside the kAmbisonicReferenceGain fix (AmbisonicEncoding.h,
  // 1/sqrt(2) -> 1.0): the W (ACN0) term of every speaker's decode dot
  // product is (encode gain) * (decode gain), both now kAmbisonicReferenceGain
  // instead of kAmbisonicReferenceGain_old - a factor-of-(1/sqrt(2))^-2 = 2x
  // increase in that one term specifically. Worst case is a fully diffuse/
  // unpositioned voice (computeAmbisonicGains' distance<=0 branch, common -
  // most tracks never set a position - returns W-only, every other channel
  // exactly 0), where the per-speaker signal is *entirely* that W term, so
  // it doubles outright; halving this constant keeps the same peak headroom
  // for that case that existed before the fix. This is an analytically
  // derived bound, not a re-tuned-by-ear value - listen and adjust if it
  // now sounds too quiet for typical (non-diffuse) content.
  static constexpr float kMasterGainTrim = 0.175f;
};

#endif
