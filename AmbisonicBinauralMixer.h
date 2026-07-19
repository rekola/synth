#ifndef _AMBISONICBINAURALMIXER_H_
#define _AMBISONICBINAURALMIXER_H_

#include "Mixer.h"
#include "AmbisonicEncoding.h"

#include <vector>

struct MYSOFA_EASY;

// Binaural ambisonic decoder (only compiled when SYNTH_HAVE_LIBMYSOFA is
// defined - see CMakeLists.txt): decodes the FOA bus to a fixed 8-speaker
// virtual cube, convolves each speaker/ear pair with an HRIR loaded via
// libmysofa (resampled to the engine's sample rate, per-ear onset delay
// preserved), sums to stereo. Falls back to AmbisonicStereoMixer
// (MixerFactory.cpp) if isReady() is false - no SOFA file resolved.
class AmbisonicBinauralMixer : public Mixer {
 public:
  AmbisonicBinauralMixer(int ambisonic_channels, int outSampleRate);
  ~AmbisonicBinauralMixer() override;

  bool isReady() const { return ready_; }

  void reset() override;
  void accumulate(const SampleData & input) override;
  SampleData encode() override;

 private:
  struct SpeakerFilter {
    FoaGains decode_gains;
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

  // Headroom against summing 8 speakers' worth of HRIR energy.
  static constexpr float kMasterGainTrim = 0.35f;
};

#endif
