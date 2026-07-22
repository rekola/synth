#ifndef _FDNREVERB_H_
#define _FDNREVERB_H_

#include "BusEffect.h"

#include <array>
#include <vector>

// Feedback delay network (FDN) reverb: 8 mutually-decorrelated delay
// lines coupled by a lossless Householder feedback matrix, each with its
// own one-pole damping filter and RT60-derived feedback gain, fed through
// a shared pre-delay and a short input-diffusion allpass cascade. Used as
// the shared send bus's spatial reverb (see SendBusProcessor) - its 8 tap
// outputs get encoded into the ambisonic bus at 8 fixed directions there,
// not mixed down to stereo here; this class only produces the 8 raw,
// decorrelated tap signals.
class FDNReverb : public BusEffect {
 public:
  explicit FDNReverb(int sampleRate);

  // Recomputes delay lengths/feedback gains/damping coefficient/pre-delay
  // length from new parameter values. Never reallocates - every buffer is
  // sized once, at construction, to the longest length the full parameter
  // range can ever need - so this is safe to call at any time, including
  // mid-playback (e.g. on a song change). A `size` change moves every
  // line's read offset within its already-allocated buffer, which would
  // otherwise click; process() briefly fades the tap output across such a
  // change (see kSizeChangeFadeMs in FDNReverb.cpp) - decay/damping/
  // pre-delay changes only change gains/filter coefficients, not buffer
  // read positions, so they don't need this.
  //
  // size: room-size multiplier, roughly 0.1-3.0, 1.0 = the base ~20-90ms
  // delay-line spread. decayRT60Seconds: time for the tail to decay 60dB.
  // damping: 0 (bright, no damping) - 1 (heavily damped, dark). The
  // underlying one-pole filter's actual coefficient is `1 - damping`
  // (clamped away from exactly 0 in FDNReverb.cpp - a literal 0
  // coefficient freezes the filter's state at its initial value forever,
  // which would crush the whole signal, not just the highs), so its gain
  // at Nyquist relative to DC is k = (1-damping)/(1+damping); per round
  // trip through a line, DC amplitude is scaled by the line's
  // feedbackGain g, HF by g*k. Working through the resulting decay-time
  // ratio shows HF's RT60 equals DC's RT60 exactly when k = g - i.e.
  // "high frequencies decay N times faster" is achieved (approximately -
  // g varies per line, this uses one shared damping) by solving
  // (1-damping)/(1+damping) = g for whichever g corresponds to that
  // target ratio. The default (0.1) targets "roughly twice as fast"
  // against a mid-length line at the default decay/size.
  // preDelaySeconds: 0-0.2 (200ms).
  void setParameters(float size, float decayRT60Seconds, float damping, float preDelaySeconds);

  static constexpr int kNumLines = 8;

  // Processes `frames` samples of mono input (the send-A sum) into the 8
  // tap outputs, retrievable via getTap() until the next process() call.
  // Always runs, even for silent input, so the feedback/damping state
  // stays continuous across blocks - a decaying tail from the previous
  // block must keep decaying here, not reset just because this block's
  // input happens to be silent.
  void process(const float * input, int frames) override;

  const float * getTap(int i) const { return taps_[static_cast<size_t>(i)].data(); }

 private:
  // Pre-delay: single delay line, buffer sized once to the max pre-delay
  // (200ms) regardless of the live pre-delay length in use.
  std::vector<float> predelayBuffer_;
  int predelayWritePos_ = 0;
  int predelayLength_ = 0;

  // Input diffusion: fixed (not size-dependent) short Schroeder allpasses,
  // increasing early echo density before the signal reaches the network.
  struct Allpass {
    std::vector<float> buffer;
    int pos = 0;
    float gain = 0.0f;
  };
  std::array<Allpass, 3> diffusers_;

  // The 8 FDN lines. buffer is sized once, at construction, to the
  // longest length the maximum supported `size` can ever need; length is
  // the *live* (current-size) delay length actually used to compute each
  // line's read offset within that same buffer - changing it never
  // touches the allocation.
  struct Line {
    std::vector<float> buffer;
    int writePos = 0;
    int length = 0;
    float feedbackGain = 0.0f;
    float dampingState = 0.0f;
  };
  std::array<Line, kNumLines> lines_;
  float dampingCoef_ = 0.0f; // one-pole coefficient, shared by all lines

  // This block's tap outputs - resized (not reallocated once warmed up,
  // same reasoning as AmbisonicBinauralMixer's own scratch buffers) to
  // whatever frame count process() is actually called with.
  std::array<std::vector<float>, kNumLines> taps_;

  // Brief fade applied only to the tap output around a `size` change (see
  // setParameters()), to avoid the click a sudden read-offset jump would
  // otherwise cause - the feedback network itself keeps running at full
  // level underneath, so no energy/state is lost, only the audible jump
  // right at the moment of change is masked.
  int fadeRemaining_ = 0;
  int fadeTotal_ = 0;
  float currentSize_ = -1.0f; // forces the first setParameters() call to "change" size and size the lines
};

#endif
