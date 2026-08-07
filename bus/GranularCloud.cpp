#include "GranularCloud.h"
#include "../ParameterSource.h"

#include <cmath>

using namespace std;

namespace {

// Defaults for this class's own (spatial/bus-level) parameters - shared
// by the constructor and storeParameters()'s deviation-only comparison
// via presetValues()'s GranularPreset::DEFAULT case, below, so the two can
// never drift apart into two different notions of "default". The
// engine's own 6 parameters have their defaults on GranularEngine itself
// (dsp/GranularEngine.h's kDefault* - reused here the same way). A wide
// but front-biased (not full-sphere) direction scatter: a totally
// enveloping cloud is available by raising directionScatter, but
// shouldn't be the out-of-the-box character.
constexpr float kDefaultDirectionScatter = 60.0f;
constexpr float kDefaultCenterAzimuth = 0.0f;
constexpr float kDefaultCenterElevation = 10.0f;
constexpr float kDefaultWet = 0.35f; // roughly the reverb/delay ballpark - see BusEffect.h

// Uses BusEffect's own generic chain-send default (0.3), same as delay -
// deliberately, not an oversight. An earlier version of this file zeroed
// this out, reasoning that a dense, continuous stream of grains chained
// into a slot A reverb would keep its feedback network re-excited,
// building into an audible multi-second smear rather than the delay
// case's occasional-echo-gets-a-halo situation. That reasoning turned out
// not to hold up: measured against a real render (a sustained note,
// chainSend=0.3 vs 0.0, comparing windowed RMS of the difference over 14
// seconds), the reverb's extra energy from the granular feed reaches
// steady state within about a second and then stays flat - a bounded,
// well-behaved room tone, not a runaway buildup. The actual "several
// seconds before the effect does anything, echoes recent material"
// reports from that period turned out to be caused by real bugs elsewhere
// (a grain read-position/capture-buffer wrap desync, an off-by-one
// lookback, and a missing catch-up floor - see triggerGrain()'s and
// process()'s own comments in dsp/GranularEngine.cpp), not by chain send.
// Grains are spatially-encoded events happening in the same simulated
// room as everything else on the bus (this class's own class comment) -
// the room's reverb coloring them a little by default is the physically
// sensible behavior, same as it already is for delay.
constexpr float kDefaultChainSend = BusEffect::kDefaultChainSendLevel;

// Fixed, not derived from song state - this is a shared bus effect,
// constructed once per song, not per voice/note, so there is no
// per-instance getRandF() seed to draw from the way per-voice noise
// (NoiseVoice, InstrumentVoice) uses. A deterministic seed makes a
// rendered song (and this class's own tests) reproduce exactly.
constexpr uint32_t kDirectionScatterSeed = 0x6d2b79f5u;

// The 9 setParameters() values plus this class's own tuned wet level, for
// each named preset (see GranularPreset in GranularCloud.h) - used both
// by loadParameters() (as the fallback default for any attribute not
// explicitly present) and storeParameters() (as the deviation-only
// comparison baseline), so a song that just writes `preset="cloud"` with
// no further overrides round-trips as exactly that, nothing more. What
// each preset is going for is documented for the user in
// docs/bus_effects.md, not repeated here.
struct PresetValues {
  float grainSizeMs, density, scanPosition, scanJitter, pitchScatter,
        directionScatter, azimuth, elevation, amplitudeJitter, wet;
};

PresetValues presetValues(GranularPreset preset) {
  switch (preset) {
  case GranularPreset::DEFAULT:
    return { GranularEngine::kDefaultGrainSizeMs, GranularEngine::kDefaultDensity,
              GranularEngine::kDefaultScanPosition, GranularEngine::kDefaultScanJitter,
              GranularEngine::kDefaultPitchScatter, kDefaultDirectionScatter,
              kDefaultCenterAzimuth, kDefaultCenterElevation,
              GranularEngine::kDefaultAmplitudeJitter, kDefaultWet };
  // Every non-default preset's grainSize/density pair is chosen so
  // density*grainSize (overlap) sits at or above dsp/GranularEngine.cpp's
  // own kMinOverlapFactor (2.5) - GranularEngine::setParameters() enforces
  // this as a hard floor regardless, but these were derived to already
  // satisfy it, not to lean on the floor to rescue an under-tuned pair.
  // See docs/bus_effects.md's granular section for the per-parameter
  // reasoning behind every number below.
  case GranularPreset::SHIMMER:
    return { 25.0f, 100.0f, 0.0f, 0.03f, 60.0f, 15.0f, 0.0f, 20.0f, 0.15f, 0.3f };
  case GranularPreset::CLOUD:
    return { 70.0f, 50.0f, 0.1f, 0.4f, 35.0f, 160.0f, 0.0f, 0.0f, 0.2f, 0.4f };
  case GranularPreset::GLITCH:
    return { 25.0f, 100.0f, 0.0f, 0.005f, 1000.0f, 10.0f, 0.0f, 0.0f, 0.6f, 0.45f };
  case GranularPreset::WASH:
    return { 180.0f, 15.0f, 0.5f, 1.2f, 10.0f, 130.0f, 0.0f, 0.0f, 0.15f, 0.45f };
  case GranularPreset::SCATTER:
    return { 50.0f, 55.0f, 0.2f, 0.8f, 250.0f, 80.0f, 0.0f, 0.0f, 0.3f, 0.35f };
  }
  // Unreachable given the switch above is exhaustive over every defined
  // GranularPreset value - only reachable via an out-of-range enum value,
  // which shouldn't occur in practice (loadParameters() below only ever
  // assigns a named enumerator). Falls back to the same values as DEFAULT.
  return { GranularEngine::kDefaultGrainSizeMs, GranularEngine::kDefaultDensity,
            GranularEngine::kDefaultScanPosition, GranularEngine::kDefaultScanJitter,
            GranularEngine::kDefaultPitchScatter, kDefaultDirectionScatter,
            kDefaultCenterAzimuth, kDefaultCenterElevation,
            GranularEngine::kDefaultAmplitudeJitter, kDefaultWet };
}

}

GranularCloud::GranularCloud(int sampleRate)
  : BusEffect(sampleRate, kDefaultWet, kDefaultChainSend),
    engine_(sampleRate),
    directionRng_(kDirectionScatterSeed) {
  directionScatterDegrees_ = kDefaultDirectionScatter;
  centerAzimuth_ = kDefaultCenterAzimuth;
  centerElevation_ = kDefaultCenterElevation;
}

void
GranularCloud::setParameters(float grainSizeMs, float densityPerSec,
                              float scanPositionSeconds, float scanJitterSeconds,
                              float pitchScatterCents, float directionScatterDegrees,
                              float centerAzimuth, float centerElevation,
                              float amplitudeJitter) {
  engine_.setParameters(grainSizeMs, densityPerSec, scanPositionSeconds, scanJitterSeconds,
                         pitchScatterCents, amplitudeJitter);

  if (directionScatterDegrees < 0.0f) directionScatterDegrees = 0.0f;
  if (directionScatterDegrees > 180.0f) directionScatterDegrees = 180.0f;

  directionScatterDegrees_ = directionScatterDegrees;
  centerAzimuth_ = centerAzimuth;
  centerElevation_ = centerElevation;
}

SphericalPosition
GranularCloud::computeDirection() {
  // Independent azimuth/elevation jitter around the center, both scaled
  // by the same directionScatterDegrees_ magnitude - a deliberately
  // simple approximation, not solid-angle-exact uniform sampling over a
  // spherical cap (which would need az/el to be jointly, not
  // independently, distributed to avoid bunching near the poles at very
  // wide scatter) - acceptable given this class's own direction scatter
  // is a creative/textural control, not a physically-modeled one.
  float az = centerAzimuth_ + directionRng_.next() * directionScatterDegrees_;
  float el = centerElevation_ + directionRng_.next() * directionScatterDegrees_;
  if (el > 90.0f) el = 90.0f;
  if (el < -90.0f) el = -90.0f;
  az = fmodf(az + 180.0f, 360.0f);
  if (az < 0.0f) az += 360.0f;
  az -= 180.0f;
  return SphericalPosition{ az, el, 1.0f };
}

void
GranularCloud::process(const float * monoInput, int frames) {
  engine_.process(monoInput, frames);

  for (int g = 0; g < kMaxSimultaneousGrains; g++) {
    if (!engine_.isGrainActive(g)) continue;

    int generation = engine_.getGrainGeneration(g);
    if (generation != lastSeenGeneration_[static_cast<size_t>(g)]) {
      lastSeenGeneration_[static_cast<size_t>(g)] = generation;
      directions_[static_cast<size_t>(g)] = computeDirection();
    }
  }
}

void
GranularCloud::loadParameters(const ParameterSource & input) {
  BusEffect::loadParameters(input); // wet/chainSend, generically

  auto preset_text = input.getText("preset");
  if (preset_text == "shimmer") preset_ = GranularPreset::SHIMMER;
  else if (preset_text == "cloud") preset_ = GranularPreset::CLOUD;
  else if (preset_text == "glitch") preset_ = GranularPreset::GLITCH;
  else if (preset_text == "wash") preset_ = GranularPreset::WASH;
  else if (preset_text == "scatter") preset_ = GranularPreset::SCATTER;
  // Absent (a bare <granular/>) and the explicit synonym "default" both
  // resolve here - see GranularPreset::DEFAULT's own doc comment in
  // GranularCloud.h for why writing it back out is still silent either way.
  else preset_ = GranularPreset::DEFAULT;

  PresetValues d = presetValues(preset_);

  setParameters(
    input.getFloat("grainSize", d.grainSizeMs),
    input.getFloat("density", d.density),
    input.getFloat("scanPosition", d.scanPosition),
    input.getFloat("scanJitter", d.scanJitter),
    input.getFloat("pitchScatter", d.pitchScatter),
    input.getFloat("directionScatter", d.directionScatter),
    input.getFloat("azimuth", d.azimuth),
    input.getFloat("elevation", d.elevation),
    input.getFloat("amplitudeJitter", d.amplitudeJitter));
  setFreeze(input.getBool("freeze", false));

  // A preset also implies its own tuned wet level - BusEffect::loadParameters()
  // above already applied "wet", but using this class's flat kDefaultWet
  // as its fallback, which only coincides with d.wet for GranularPreset::
  // DEFAULT itself. Redo it here (harmless, exactly a no-op, for DEFAULT)
  // so an explicit wet="..." attribute still wins, but an absent one falls
  // back to the resolved preset's own wet rather than the generic default.
  // (storeParameters() below may then write an explicit "wet" attribute
  // even for a resolved-preset instance whose wet exactly matches the
  // preset - BusEffect's own generic wet handling has no notion of presets
  // to diff against instead - harmless: reloading that XML reproduces the
  // identical sound either way.)
  setWetLevel(input.getFloat("wet", d.wet));
}

void
GranularCloud::storeParameters(ParameterSource & output) const {
  BusEffect::storeParameters(output); // wet/chainSend, generically

  // Deviation-only - GranularPreset::DEFAULT's to_string() is ""
  // specifically so this stays quiet for it, matching every other
  // implicit-default attribute this class writes below.
  if (preset_ != GranularPreset::DEFAULT) output.set("preset", to_string(preset_));

  PresetValues d = presetValues(preset_);
  output.set("grainSize", getGrainSizeMs(), d.grainSizeMs);
  output.set("density", getDensity(), d.density);
  output.set("scanPosition", getScanPosition(), d.scanPosition);
  output.set("scanJitter", getScanJitter(), d.scanJitter);
  output.set("pitchScatter", getPitchScatter(), d.pitchScatter);
  output.set("directionScatter", getDirectionScatter(), d.directionScatter);
  output.set("azimuth", getCenterAzimuth(), d.azimuth);
  output.set("elevation", getCenterElevation(), d.elevation);
  output.set("amplitudeJitter", getAmplitudeJitter(), d.amplitudeJitter);
  if (getFreeze()) output.set("freeze", 1);
}
