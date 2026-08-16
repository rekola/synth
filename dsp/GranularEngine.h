#ifndef _GRANULARENGINE_H_
#define _GRANULARENGINE_H_

#include "NoiseGenerator.h"

#include <array>
#include <vector>

// Granular synthesis engine: captures a mono input into a rolling ring
// buffer and scatters short windowed slices ("grains") of it back out,
// each with its own onset offset, duration, playback rate (pitch), and
// amplitude.
//
// Deliberately has no notion of space, direction, or multi-channel output
// - it produces up to kMaxSimultaneousGrains independent mono "voices"
// (getTap()/isGrainActive(), one per slot, each pinned to a fixed slot
// index for its whole life), so any caller can build whatever channel or
// spatial meaning it needs on top without this class knowing anything
// about it. Currently used by the shared send bus's granular cloud effect
// (bus/GranularCloud.h), which assigns each active slot a direction in
// space via getGrainGeneration() below; a future plain granular
// instrument could just as well sum every active tap into one mono/
// stereo voice and never look at generation at all. This is why the code
// lives here, in dsp/, rather than in bus/ - dsp/ is reusable,
// dependency-free DSP building blocks that bus/ (and, so far, only bus/)
// depends on, never the other way around.
class GranularEngine {
 public:
  explicit GranularEngine(int sampleRate);

  // Hard cap, not a parameter - grains_/taps_ are sized to this once, at
  // construction, matching every bus/ effect's "everything preallocated,
  // nothing reallocated after load" convention (which this class also
  // honors, even though it isn't a BusEffect itself).
  static constexpr int kMaxSimultaneousGrains = 64;

  // Defaults - shared by the constructor (so a standalone GranularEngine
  // already sounds sensible before any setParameters() call) and any
  // caller's own deviation-only project-file saving (see
  // bus/GranularCloud.cpp), so the two can never drift apart into two
  // different notions of "default". Exposed publicly (unlike the
  // internal-only min/max clamps in GranularEngine.cpp) specifically so
  // a caller doing deviation-only saving of its own has a single shared
  // source of truth to compare against, rather than a second copy of the
  // same numbers that could silently drift.
  static constexpr float kDefaultGrainSizeMs = 60.0f;
  static constexpr float kDefaultDensity = 45.0f;
  static constexpr float kDefaultScanPosition = 0.0f;
  static constexpr float kDefaultScanJitter = 0.2f;
  static constexpr float kDefaultPitchScatter = 40.0f;
  static constexpr float kDefaultAmplitudeJitter = 0.25f;

  // grainSizeMs: 10-200ms, a grain's duration. densityPerSec: average
  // grain trigger rate (grains/sec, overlapping - not a ceiling on
  // simultaneous grains, which is kMaxSimultaneousGrains above).
  // scanPositionSeconds/scanJitterSeconds: how far back from the live
  // write head a grain starts reading, base + uniform jitter.
  // pitchScatterCents: per-grain playback-rate randomization, +-cents.
  // amplitudeJitter: 0-1, per-grain amplitude randomization as a fraction
  // of unity. Never reallocates - buffers are sized once, at
  // construction, to the widest range these can ever need - so this is
  // safe to call at any time, including mid-playback.
  void setParameters(float grainSizeMs, float densityPerSec,
                      float scanPositionSeconds, float scanJitterSeconds,
                      float pitchScatterCents, float amplitudeJitter);

  // Freeze: stop advancing the capture buffer, keep granulating whatever
  // it already holds - a sustained cloud from a snapshot of material,
  // rather than continuously-arriving input. Trigger semantics (how this
  // is invoked - a command, a MIDI CC, ...) are out of scope here; this
  // is just the engine-level state toggle.
  void setFreeze(bool freeze) { freeze_ = freeze; }
  bool getFreeze() const { return freeze_; }

  // Read back setParameters()'s (clamped) values - used only for
  // deviation-only project-file saving by callers, not by any DSP code
  // here.
  float getGrainSizeMs() const { return grainSizeMs_; }
  float getDensity() const { return densityPerSec_; }
  float getScanPosition() const { return scanPositionSeconds_; }
  float getScanJitter() const { return scanJitterSeconds_; }
  float getPitchScatter() const { return pitchScatterCents_; }
  float getAmplitudeJitter() const { return amplitudeJitter_; }

  // Processes `frames` samples of mono input into the capture buffer
  // (unless frozen) and runs the grain scheduler. Always runs, even for
  // silent input, so the scheduler's phase and every active grain's own
  // playback position stay continuous across blocks.
  void process(const float * monoInput, int frames);

  const float * getTap(int i) const { return taps_[static_cast<size_t>(i)].data(); }
  bool isGrainActive(int i) const { return grains_[static_cast<size_t>(i)].active; }

  // Increments every time slot `i` is (re)triggered - a caller that wants
  // to attach its own per-grain metadata keyed by slot index (e.g. a
  // spatial direction - see bus/GranularCloud.h) can compare this against
  // whatever value it last saw for that slot to tell "this is a new grain
  // since I last looked, (re)compute my own metadata for it" without this
  // class needing to know what that metadata even is.
  int getGrainGeneration(int i) const { return grains_[static_cast<size_t>(i)].generation; }

  // Exposed purely so tests can verify scheduling/buffer-wrap behavior
  // directly, the same "debug accessor added purely for tests" precedent
  // bus/MultiTapDelay.h's getFeedbackGainMultiplier() already establishes.
  int getGrainsTriggeredForTest() const { return grainsTriggeredForTest_; }
  double getGrainReadPosForTest(int i) const { return grains_[static_cast<size_t>(i)].readPos; }
  int getCaptureWritePosForTest() const { return captureWritePos_; }
  int getCaptureBufferSizeForTest() const { return static_cast<int>(captureBuffer_.size()); }

 private:
  struct Grain {
    bool active = false;
    // Fractional read position into captureBuffer_, wraps mod its size -
    // double, not float: this accumulates via += every sample for a
    // grain's whole life, and its magnitude scales with captureBuffer_'s
    // size (kMaxCaptureSeconds * sample rate, unbounded - there's no
    // ceiling on --samplerate), so float's absolute precision at that
    // magnitude (~0.03 samples/op at 44.1kHz, worse at higher rates)
    // isn't reliably enough headroom below a single sample after
    // thousands of accumulations; double's is, by many orders of
    // magnitude, at any realistic sample rate.
    double readPos = 0.0;
    int lengthSamples = 0;   // this grain's duration, resolved from grainSizeMs_ at trigger time
    int age = 0;             // samples emitted so far - drives the window lookup and stealing priority
    float rate = 1.0f;       // playback-rate ratio from pitch scatter (cents -> 2^(cents/1200))
    float amplitude = 1.0f;
    int generation = 0;      // bumped every (re)trigger - see getGrainGeneration()
  };

  void triggerGrain();
  int findFreeOrStealGrainSlot();
  float sampleWindow(float t) const;
  int getSampleRate() const { return sampleRate_; }

  int sampleRate_;

  std::array<Grain, kMaxSimultaneousGrains> grains_;
  std::array<std::vector<float>, kMaxSimultaneousGrains> taps_;

  std::vector<float> captureBuffer_;
  int captureWritePos_ = 0;
  bool freeze_ = false;

  // How many samples of *real* audio captureBuffer_ actually holds so
  // far, saturating at its own size once it's been written all the way
  // around once - distinct from captureBuffer_.size() (its physical
  // capacity, all of which starts zero-initialized). triggerGrain()
  // clamps a grain's scan-back distance to this, not just to physical
  // capacity, so a grain never reads from a not-yet-written (silent)
  // stretch of the buffer - without this, a song with a nontrivial
  // scanPosition/scanJitter would grow silent, thin-sounding grains for
  // as long as it takes real capture to catch up to the requested depth,
  // rather than always reading whatever real history already exists.
  int capturedSamples_ = 0;

  std::vector<float> windowTable_;

  // Seeded once, with a fixed constant, not from a per-note NoteCoordinate
  // (this engine is constructed once per bus slot/instrument voice, not
  // per note) - deterministic on purpose, so a rendered song (and this
  // class's own tests) reproduce exactly across runs, matching
  // NoiseGenerator's own "each instance seeded once" convention.
  NoiseGenerator scatterRng_;

  // Accumulates toward the next grain onset - see triggerGrain()'s caller
  // in process().
  float schedulerPhase_ = 0.0f;

  float grainSizeMs_;
  float densityPerSec_;
  float scanPositionSeconds_;
  float scanJitterSeconds_;
  float pitchScatterCents_;
  float amplitudeJitter_;

  int grainsTriggeredForTest_ = 0;
};

#endif
