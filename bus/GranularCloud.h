#ifndef _GRANULARCLOUD_H_
#define _GRANULARCLOUD_H_

#include "BusEffect.h"
#include "../dsp/GranularEngine.h"
#include "../dsp/NoiseGenerator.h"

#include <array>

// Named parameter sets showcasing the effect (see bus/GranularCloud.cpp's
// presetValues() for the exact numbers and this class's own loadParameters()
// for how a preset interacts with individually-specified attributes) -
// same shape as bus/FDNReverb.h's FDNReverbPreset. DEFAULT is itself a
// named, described preset (see presetValues()) - the one a bare
// `<granular/>`, or an explicit `preset="default"`, resolves to - not just
// an unnamed fallback; to_string() still maps it to "" the same way
// FDNReverbPreset::DEFAULT does, so a default-preset instance round-trips
// quietly, with no explicit preset="..." attribute written, matching every
// other implicit-default attribute in this codebase.
enum class GranularPreset { DEFAULT = 0, SHIMMER, CLOUD, GLITCH, WASH, SCATTER };

static inline const std::string to_string(GranularPreset preset) {
  switch (preset) {
  case GranularPreset::DEFAULT: return "";
  case GranularPreset::SHIMMER: return "shimmer";
  case GranularPreset::CLOUD: return "cloud";
  case GranularPreset::GLITCH: return "glitch";
  case GranularPreset::WASH: return "wash";
  case GranularPreset::SCATTER: return "scatter";
  }
  return "";
}

// Spatial wrapper around dsp/GranularEngine.h's reusable grain-synthesis
// core: assigns each currently-sounding grain a fixed direction in space
// (getTapDirection()) for its whole life, the same way FDNReverb's 8 lines
// or MultiTapDelay's 4 taps are each pinned to one direction - just with
// many more, much shorter, dynamically-triggered voices instead of a
// handful of fixed ones. All of the actual grain scheduling/capture/
// windowing DSP lives in the engine (dsp/GranularEngine.h), kept
// spatially-agnostic there so it can also serve a future non-spatial
// granular instrument; this class's only job is "where does each of the
// engine's grain voices point," plus the BusEffect/XML integration
// (wet/chainSend, presets) below.
//
// Up to kMaxSimultaneousGrains grains sound at once, each pinned to a
// fixed tap index (getTap(i)/getTapDirection(i)) for its entire lifetime -
// SendBusProcessor's per-tap AmbisonicVoiceEncoder array (one per index)
// therefore only ever interpolates across a grain's onset/decay, never a
// direction *change* mid-grain (a grain's direction, unlike
// MultiTapDelay's feedback tap, never moves once triggered).
class GranularCloud : public BusEffect {
 public:
  explicit GranularCloud(int sampleRate);

  static constexpr int kMaxSimultaneousGrains = GranularEngine::kMaxSimultaneousGrains;

  // The engine's own 6 parameters (grain size/density/scan position &
  // jitter/pitch scatter/amplitude jitter, forwarded to
  // dsp::GranularEngine::setParameters() as-is) plus this class's own
  // spatial ones: directionScatterDegrees: per-grain direction
  // randomization around (centerAzimuth, centerElevation), +-degrees on
  // each axis (see bus/GranularCloud.cpp's computeDirection() for the
  // exact, deliberately approximate - not solid-angle-exact - sampling
  // this implements). Never reallocates - buffers are sized once, at
  // construction, to the widest range these can ever need - so this is
  // safe to call at any time, including mid-playback.
  void setParameters(float grainSizeMs, float densityPerSec,
                      float scanPositionSeconds, float scanJitterSeconds,
                      float pitchScatterCents, float directionScatterDegrees,
                      float centerAzimuth, float centerElevation,
                      float amplitudeJitter);

  // Freeze: stop advancing the capture buffer, keep granulating whatever
  // it already holds - a sustained positional cloud from a snapshot of
  // material, rather than continuously-arriving input. Trigger semantics
  // (how this is invoked - a command, a MIDI CC, ...) are out of scope
  // here; this is just a passthrough to the engine's own state toggle.
  // Kept separate from setParameters() (unlike the other 9 parameters
  // above) so a future freeze-toggle command doesn't need to know or
  // re-supply every other parameter just to flip this one bit.
  void setFreeze(bool freeze) { engine_.setFreeze(freeze); }
  bool getFreeze() const { return engine_.getFreeze(); }

  // Read back setParameters()'s (clamped) values - used only for
  // deviation-only project-file saving (storeParameters() below), not by
  // any DSP code here.
  float getGrainSizeMs() const { return engine_.getGrainSizeMs(); }
  float getDensity() const { return engine_.getDensity(); }
  float getScanPosition() const { return engine_.getScanPosition(); }
  float getScanJitter() const { return engine_.getScanJitter(); }
  float getPitchScatter() const { return engine_.getPitchScatter(); }
  float getDirectionScatter() const { return directionScatterDegrees_; }
  float getCenterAzimuth() const { return centerAzimuth_; }
  float getCenterElevation() const { return centerElevation_; }
  float getAmplitudeJitter() const { return engine_.getAmplitudeJitter(); }
  GranularPreset getPreset() const { return preset_; }

  // <granular> element attributes: "preset" plus the 9 setParameters()
  // values and freeze, each deviation-only against whatever the resolved
  // preset (or, absent one, this constructor's own tuned defaults - see
  // GranularCloud.cpp's presetValues()) implies - wet/chainSend are
  // handled generically by BusEffect::loadParameters()/storeParameters(),
  // called first.
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  // Processes `frames` samples of mono input (this slot's send sum)
  // through the engine, then assigns a direction to any grain the engine
  // (re)triggered this block (detected via getGrainGeneration() - see
  // dsp/GranularEngine.h).
  void process(const float * monoInput, int frames) override;

  int getNumTaps() const override { return kMaxSimultaneousGrains; }
  const float * getTap(int i) const override { return engine_.getTap(i); }
  SphericalPosition getTapDirection(int i) const override { return directions_[static_cast<size_t>(i)]; }

  // Exposed purely so tests can verify scheduling/direction-scatter
  // behavior directly, the same "debug accessor added purely for tests"
  // precedent MultiTapDelay::getFeedbackGainMultiplier() already
  // establishes.
  int getGrainsTriggeredForTest() const { return engine_.getGrainsTriggeredForTest(); }
  bool isGrainActiveForTest(int i) const { return engine_.isGrainActive(i); }
  double getGrainReadPosForTest(int i) const { return engine_.getGrainReadPosForTest(i); }
  int getCaptureWritePosForTest() const { return engine_.getCaptureWritePosForTest(); }
  int getCaptureBufferSizeForTest() const { return engine_.getCaptureBufferSizeForTest(); }

 private:
  SphericalPosition computeDirection();

  GranularEngine engine_;

  std::array<SphericalPosition, kMaxSimultaneousGrains> directions_ {};
  // Last generation (dsp::GranularEngine::getGrainGeneration()) seen for
  // each slot - a mismatch against the engine's current value means that
  // slot was (re)triggered since process() last looked, so it needs a
  // freshly computed direction. Initialized to 0 to match Grain's own
  // initial generation, so the very first real trigger on a slot (which
  // bumps it to 1) is always detected as new.
  std::array<int, kMaxSimultaneousGrains> lastSeenGeneration_ {};

  // Seeded once, with a fixed constant, not from a per-note NoteCoordinate
  // (this is a shared bus effect, constructed once per song, not per
  // note) - deterministic on purpose, so a rendered song (and this
  // class's own tests) reproduce exactly across runs, matching
  // dsp/NoiseGenerator.h's own "each instance seeded once" convention.
  // Separate from the engine's own scatterRng_ (dsp/GranularEngine.h) -
  // direction is this class's own concern, not the engine's.
  NoiseGenerator directionRng_;

  GranularPreset preset_ { GranularPreset::DEFAULT };

  float directionScatterDegrees_;
  float centerAzimuth_;
  float centerElevation_;
};

#endif
