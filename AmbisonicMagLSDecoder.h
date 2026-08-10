#ifndef _AMBISONICMAGLSDECODER_H_
#define _AMBISONICMAGLSDECODER_H_

#include "Mixer.h"
#include "AmbisonicEncoding.h"

#include <vector>

// MagLS (magnitude least-squares) binaural ambisonic decoder - only
// compiled when SYNTH_HAVE_LIBMYSOFA is defined (see CMakeLists.txt),
// same as AmbisonicBinauralMixer. Unlike that class's virtual-speaker rig
// (decode to a handful of directions, convolve each against a measured
// HRIR), this precomputes exactly one HRIR-equivalent filter pair per
// ambisonic channel (2*(order+1)^2 total - 32 at order 3), solved once at
// load time to best reproduce the *entire* measured HRTF set on its own
// native measurement grid: phase-accurate below a transition frequency
// (preserving ITD), magnitude-accurate with continuously propagated phase
// above it (removing the comb-filtering/coloration a finite speaker rig
// can't avoid). See docs/known_bugs.md or the project's own plan history
// for the full derivation - summarized here at the call sites.
//
// Deliberately a sibling class, not a modification of
// AmbisonicBinauralMixer: it never calls maxReGainsPerDegree()/acnDegree()
// (max-rE weighting is specific to the virtual-speaker decode - MagLS's
// least-squares solve already optimizes the whole bus-to-ears projection,
// so applying max-rE ahead of it would double-weight and blur exactly
// what MagLS is solving for) and touches none of AmbisonicBinauralMixer's
// own state. Both classes can coexist; see MixerFactory for selection.
class AmbisonicMagLSDecoder : public Mixer {
 public:
  AmbisonicMagLSDecoder(int ambisonic_channels, int outSampleRate);
  ~AmbisonicMagLSDecoder() override;

  bool isReady() const { return ready_; }

  void reset() override;
  void accumulate(const AudioBuffer & input) override;
  AudioBuffer encode() override;

  const AudioBuffer & getRawBus() const override { return buffer_; }

  // Read-only access to the solved per-channel filter pairs, for the
  // Phase 1 validation suite (ITD/magnitude/symmetry/diffuse-gain checks)
  // to inspect directly without a full real-time encode() round trip.
  int numberOfChannels() const { return ambisonic_channels_; }
  const std::vector<float> & leftFilter(int channel) const { return channel_filters_[static_cast<size_t>(channel)].left; }
  const std::vector<float> & rightFilter(int channel) const { return channel_filters_[static_cast<size_t>(channel)].right; }
  int filterSampleRate() const { return getOutSampleRate(); }

 private:
  struct ChannelFilter {
    std::vector<float> left, right;
  };

  int ambisonic_channels_;
  bool ready_ = false;
  std::vector<ChannelFilter> channel_filters_; // size ambisonic_channels_, one pair per ACN channel

  AudioBuffer buffer_;

  // Overlap-add tail carried between encode() calls, sized once at
  // construction from the solved filter length - same pattern as
  // AmbisonicBinauralMixer's own left_tail_/right_tail_.
  std::vector<float> left_tail_, right_tail_;
  std::vector<float> left_acc_, right_acc_;
};

#endif
