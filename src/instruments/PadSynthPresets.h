#ifndef _PADSYNTHPRESETS_H_
#define _PADSYNTHPRESETS_H_

#include "PadSynthWavetable.h"
#include "PartialPosition.h"
#include "ImportedPadSynthTable.h"

#include <memory>
#include <string>
#include <vector>

// Compiled-in per-preset default values, resolved once in
// PadSynth::loadParameters() - the same role TapeDegradationPresets.h
// plays for tape degradation. An explicit XML attribute always overrides
// its preset default (see PadSynth::loadParameters()'s own
// get<float>(name, preset.field) calls).
//
// amplitude_rolloff_exponent, formants and harmonic_amplitude_jitter
// extend the bare bandwidth/bandwidthScale/partials PADsynth parameters
// (see PadSynthWavetable.h's own doc comment) - without them, every
// preset could only differ by bandwidth and partial count, which can't
// tell a clean "glass" bell apart from a "formant-vocal" character, and
// can't add the kind of irregular, non-smooth per-harmonic detail a real
// instrument's own spectrum has.
struct PadSynthPresetParams {
  float bandwidth_cents;
  float bandwidth_scale_exponent;
  int partial_count;
  float amplitude_rolloff_exponent;
  std::vector<PadSynthFormant> formants;
  float harmonic_amplitude_jitter = 0.0f;
  // Non-empty replaces amplitude_rolloff_exponent's formula outright (see
  // PadSynthWavetable.h's own doc comment) - an explicit per-harmonic
  // amplitude array, index 0 = harmonic 1.
  std::vector<float> harmonic_amplitudes = {};

  // Preset-level default for the "tuningMatched" XML attribute - an
  // explicit attribute still always overrides this.
  bool tuning_matched = true;

  // The anchored spectral-envelope remap (dsp/SpectralEnvelopeRemap.h) and
  // its optional postprocess - see PadSynth.h's own doc comment for the
  // XML attribute names these mirror. envelope_anchor_hz <= 0 means off.
  float envelope_anchor_hz = 0.0f;
  float envelope_tracking = 0.0f;
  PadSynthPostprocessKind postprocess_kind = PadSynthPostprocessKind::None;
  int postprocess_n = 0;
  int postprocess_r = 0;
  float postprocess_amount = 0.0f;

  // g(h) - PartialPosition.h - preset-only, like formants/
  // harmonic_amplitudes above (ParameterSource has no array type).
  PartialPositionSpec position_spec = {};

  // Non-null selects ImportedPadSynthTable (the oscillator-chain +
  // profile-placement renderer, docs/padsynth.md) over PadSynthWavetable's
  // own Gaussian-band one for this preset entirely - every field above
  // except tuning_matched/envelope_*/postprocess_* (read the same way by
  // both renderers) is then unused.
  std::shared_ptr<ImportedPadSynthParams> imported;
};

// Short local aliases - only used to keep the oscillator-shaped preset
// data below readable; never exposed outside this file.
using OCP = ImportedOscillator::OscillatorChainParams;
using ImportedBaseFunction = ImportedOscillator::BaseFunction;
using ImportedWaveshaperKind = ImportedOscillator::WaveshaperKind;
using ImportedFilterKind = ImportedOscillator::FilterKind;
using ImportedFilterParams = ImportedOscillator::HarmonicFilterParams;
using ImportedTimeWarp = ImportedOscillator::TimeWarp;
using ImportedSpectrumAdjustKind = ImportedOscillator::SpectrumAdjustKind;
using ImportedProfileParams = ImportedPadSynth::ProfileParams;
using ImportedProfileType = ImportedPadSynth::ProfileType;
using ImportedPositionParams = ImportedPadSynth::PositionParams;

// Builds a PadSynthPresetParams for a preset that uses
// ImportedPadSynthTable's own oscillator-chain + profile-placement
// rendering (docs/padsynth.md) - every field of the outer
// PadSynthPresetParams the Gaussian-band renderer would otherwise read is
// irrelevant here except tuning_matched/envelope_*/postprocess_*, which
// both renderers read the same way (an explicit XML attribute still
// always overrides these).
inline PadSynthPresetParams importedPreset(OCP oscillator, ImportedProfileParams profile, ImportedPositionParams position,
                                            float bandwidth_cents, float base_frequency_hz, int octaves, int samples_per_octave,
                                            int table_length, bool tuning_matched,
                                            float envelope_anchor_hz = 0.0f, float envelope_tracking = 0.0f,
                                            PadSynthPostprocessKind postprocess_kind = PadSynthPostprocessKind::None,
                                            int postprocess_n = 0, int postprocess_r = 0, float postprocess_amount = 0.0f) {
  PadSynthPresetParams params;
  params.bandwidth_cents = bandwidth_cents;
  params.tuning_matched = tuning_matched;
  params.envelope_anchor_hz = envelope_anchor_hz;
  params.envelope_tracking = envelope_tracking;
  params.postprocess_kind = postprocess_kind;
  params.postprocess_n = postprocess_n;
  params.postprocess_r = postprocess_r;
  params.postprocess_amount = postprocess_amount;

  auto imported = std::make_shared<ImportedPadSynthParams>();
  imported->oscillator = std::move(oscillator);
  imported->profile = profile;
  imported->position = position;
  imported->bandwidth_cents = bandwidth_cents;
  imported->base_frequency_hz = base_frequency_hz;
  imported->octaves = octaves;
  imported->samples_per_octave = samples_per_octave;
  imported->table_length = table_length;
  imported->tuning_matched = tuning_matched;
  imported->envelope_anchor_hz = envelope_anchor_hz;
  imported->envelope_tracking = envelope_tracking;
  imported->postprocess_kind = postprocess_kind;
  imported->postprocess_n = postprocess_n;
  imported->postprocess_r = postprocess_r;
  imported->postprocess_amount = postprocess_amount;
  params.imported = std::move(imported);
  return params;
}

// An unrecognized preset name falls back to "warm" rather than asserting -
// the same "unrecognized name falls back to a real default" shape
// TapeDegradationPresets.h's own getTapeDegradationPreset() already uses.
inline const PadSynthPresetParams & getPadSynthPreset(const std::string & name) {
  // Moderate bandwidth, a natural 1/n^2 rolloff - a sensible general-
  // purpose pad and the engine's own fallback for an unrecognized name.
  static const PadSynthPresetParams kWarm{
    /* bandwidth_cents            */ 18.0f,
    /* bandwidth_scale_exponent   */ 0.6f,
    /* partial_count              */ 32,
    /* amplitude_rolloff_exponent */ 2.0f,
    /* formants                   */ {},
  };

  // "keyboard" - a sparse, few-harmonic piano tone: a power-ramp
  // oscillator shape, an arctangent waveshaper, an oscillator time warp,
  // and fractional-stretch partial positions (partial 10 lands at 10.04).
  // Tuning matching stays off - turning it on would erase the stretch
  // that gives the low partials their piano-string character. Used as a
  // PadSynth-side comparison point against the additive piano
  // (docs/additive.md): PadSynth has no per-partial decay of its own, so
  // this preset's "struck" quality comes entirely from the wrapping
  // `<envelope>`'s own decay-to-zero-sustain shape.
  static const PadSynthPresetParams kKeyboard = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::PowerRamp;
      o.base_shape_param = 2.871f;
      o.harmonics = { { 1, 0.984f }, { 2, 0.984f }, { 5, 0.547f }, { 6, 0.625f } };
      o.waveshaper_kind = ImportedWaveshaperKind::Arctangent;
      o.waveshaper_k = 46.72f;
      o.oscillator_warp = ImportedTimeWarp{ true, 0.1053f, -0.0039f, 1.0f };
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Rectangular, 6.258f, 127, true },
    ImportedPositionParams{ 6, 78, 56, 0 },
    /* bandwidth_cents */ 11.5f, /* base_frequency_hz */ 392.4f, /* octaves */ 5, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ false,
    /* envelope_anchor_hz */ 115.6f, /* envelope_tracking */ 0.980f);

  // "synth-piano-3-b" - a sibling of "keyboard": a Gaussian-pulse
  // oscillator shape, an arctangent-plus-single-harmonic-boost filter
  // chain (the same shaping family as "bells-3"), a slightly stronger
  // partial-10 stretch (10.06 vs "keyboard"'s 10.04), and its own
  // stretch-mix postprocess.
  static const PadSynthPresetParams kSynthPiano3B = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::GaussianPulse;
      o.base_shape_param = 0.5273f;
      o.harmonics = { { 1, 0.984f } };
      o.waveshaper_kind = ImportedWaveshaperKind::Arctangent;
      o.waveshaper_k = 6.247f;
      o.filter = ImportedFilterParams{ ImportedFilterKind::SingleHarmonicBoost, 0.0f, 0.0f, 1, 1.946f };
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 6.258f, 127, true },
    ImportedPositionParams{ 6, 92, 56, 0 },
    /* bandwidth_cents */ 32.5f, /* base_frequency_hz */ 392.4f, /* octaves */ 6, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ false,
    /* envelope_anchor_hz */ 233.2f, /* envelope_tracking */ 0.634f,
    /* postprocess_kind */ PadSynthPostprocessKind::StretchMix, /* postprocess_n */ 2, /* postprocess_r */ 0, /* postprocess_amount */ 0.646f);

  // A handful of vowel-like formant resonances (loosely modeled on an
  // open "ah" vowel's own F1/F2/F3, plus a "singer's formant" - a
  // well-documented acoustic feature of trained/choral voices, a real
  // F3-F4-F5 cluster merging into one bright resonance around 2.8-3.4kHz
  // that gives a voice its characteristic "ring") boost specific harmonic
  // bands regardless of fundamental, the way a vocal tract's own fixed
  // resonant cavities do. `harmonic_amplitude_jitter` adds irregular,
  // non-smooth per-harmonic detail on top - what a smooth rolloff-plus-
  // formant-bumps curve alone can't produce, and what actually tells a
  // resonant body apart from a clean synthesized curve.
  static const PadSynthPresetParams kFormantVocal{
    /* bandwidth_cents            */ 22.0f,
    /* bandwidth_scale_exponent   */ 0.6f,
    /* partial_count              */ 56,
    /* amplitude_rolloff_exponent */ 1.8f,
    /* formants                   */ {
      { /* center_hz */ 700.0f,  /* bandwidth_hz */ 120.0f, /* gain */ 6.0f },  // F1
      { /* center_hz */ 1220.0f, /* bandwidth_hz */ 160.0f, /* gain */ 4.5f },  // F2
      { /* center_hz */ 2600.0f, /* bandwidth_hz */ 220.0f, /* gain */ 3.0f },  // F3
      { /* center_hz */ 3000.0f, /* bandwidth_hz */ 350.0f, /* gain */ 4.0f },  // singer's formant (F3-F5 cluster)
    },
    /* harmonic_amplitude_jitter  */ 0.15f,
  };

  // Narrow bandwidth and a steep rolloff - most of the energy sits in the
  // fundamental and a handful of clean, minimally-beating overtones, the
  // bell/glass-like clarity a wide band would otherwise smear away.
  static const PadSynthPresetParams kGlass{
    /* bandwidth_cents            */ 8.0f,
    /* bandwidth_scale_exponent   */ 0.5f,
    /* partial_count              */ 32,
    /* amplitude_rolloff_exponent */ 1.5f,
    /* formants                   */ {},
  };

  // A Mellotron's "strings" tape was 3 real violins recorded per note -
  // this preset's own job is purely spectral character (what a massed
  // bowed string ensemble sounds like, the same harmonic content as
  // "strings" below), not literal unison/detune voices; that comes from
  // <tapeDegradation preset="mellotron"> layered on top instead (built
  // separately - see docs/tape_degradation.md). Bandwidth/rolloff stay
  // slightly narrower/steeper than "strings", reading a little more
  // "recorded tape" than a live pad.
  static const PadSynthPresetParams kMellotron{
    /* bandwidth_cents            */ 8.0f,
    /* bandwidth_scale_exponent   */ 0.45f,
    /* partial_count              */ 2,
    /* amplitude_rolloff_exponent */ 1.0f, // unused - harmonic_amplitudes below replaces it
    /* formants                   */ {},
    /* harmonic_amplitude_jitter  */ 0.0f,
    /* harmonic_amplitudes        */ { 127.0f/127.0f, 117.0f/127.0f },
  };

  // "church-organ" - a pipe organ's flue pipes are about as close to
  // PADsynth's own idealized case as a real instrument gets: a steady,
  // essentially beat-free harmonic stack with no per-note decay of its
  // own. A clipped-triangle oscillator shape against 8 harmonics, plus an
  // exponential-lowpass harmonic filter, gives this preset its drawbar-
  // organ-style spectrum. Used as a fallback at organ.pipe (a real
  // acoustic organ from a loaded instrument sample library always wins;
  // this only fills the leaf when none is available).
  static const PadSynthPresetParams kChurchOrgan = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::ClippedTriangle;
      o.base_shape_param = 0.8477f;
      o.harmonics = { { 1, 0.984f }, { 2, 0.734f }, { 4, 0.500f }, { 8, 0.438f },
                       { 12, 0.156f }, { 16, 0.469f }, { 24, 0.188f }, { 32, 0.156f } };
      o.filter = ImportedFilterParams{ ImportedFilterKind::ExponentialLowpass, 0.977975f, 0.032346f, 0, 1.0f };
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 21.72f, 127, true },
    ImportedPositionParams{ 0, 0, 0, 0 },
    /* bandwidth_cents */ 5.2f, /* base_frequency_hz */ 261.6f, /* octaves */ 4, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ true);

  // "bells" - base_function "none" places 3 harmonics (1/2/4) directly,
  // then a logistic-sigmoid waveshaper reshapes that sparse set
  // nonlinearly, filling in the rest of the spectrum - the small handful
  // of dominant partials plus that reshaping is what actually makes this
  // read as a bell rather than a generic tone. Partial positions land at
  // 1,2,4,5,7,9,11,13,... rather than a plain harmonic series - a real
  // bell's strike-tone partials aren't simple integer multiples. The
  // preset's own default is tuning-matched on; a song that wants the bare,
  // untempered dissonance instead (exact-integer partials read as
  // "out of tune" against the engine's own scale-quantized tuning the
  // same way a real bell's non-integer partials read as dissonant against
  // a fixed pitch) sets tuningMatched="false" explicitly.
  static const PadSynthPresetParams kBells = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::None;
      o.harmonics = { { 1, 0.984f }, { 2, 0.172f }, { 4, 0.313f } };
      o.waveshaper_kind = ImportedWaveshaperKind::LogisticSigmoid;
      o.waveshaper_k = 8.443f;
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 6.258f, 127, true },
    ImportedPositionParams{ 6, 255, 75, 255 },
    /* bandwidth_cents */ 21.2f, /* base_frequency_hz */ 392.4f, /* octaves */ 5, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ true,
    /* envelope_anchor_hz */ 233.2f, /* envelope_tracking */ 0.634f,
    /* postprocess_kind */ PadSynthPostprocessKind::ResidueClassWeighting,
    /* postprocess_n */ 2, /* postprocess_r */ 1, /* postprocess_amount */ 0.646f);

  // "strings" - a power-ramp oscillator shape against 2 harmonics. A very
  // simple, near-octave-doubled tone on its own - real ensemble character
  // comes from unison layering on top (InstrumentLibrary.cpp's own
  // string.synth.slow/pad.bowed registrations).
  static const PadSynthPresetParams kStrings = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::PowerRamp;
      o.base_shape_param = 0.3766f;
      o.harmonics = { { 1, 0.984f }, { 2, 0.828f } };
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 6.258f, 127, true },
    ImportedPositionParams{ 0, 0, 0, 0 },
    /* bandwidth_cents */ 57.3f, /* base_frequency_hz */ 261.6f, /* octaves */ 6, /* samples_per_octave */ 2,
    /* table_length */ 1 << 18, /* tuning_matched */ true);

  // "dual-strings" - the same power-ramp oscillator shape as "strings",
  // against just harmonic 1 and a wider bandwidth of its own. Its own
  // "dual"/octave-doubled character comes from unison layering on top,
  // same as "strings" - see InstrumentLibrary.cpp's own pad.bowed/
  // PadSynth Dual Strings <multiply> wrapping.
  static const PadSynthPresetParams kDualStrings = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::PowerRamp;
      o.base_shape_param = 0.3766f;
      o.harmonics = { { 1, 0.984f } };
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 6.258f, 127, true },
    ImportedPositionParams{ 0, 0, 0, 0 },
    /* bandwidth_cents */ 101.5f, /* base_frequency_hz */ 261.6f, /* octaves */ 6, /* samples_per_octave */ 2,
    /* table_length */ 1 << 18, /* tuning_matched */ true);

  // "saw-piano" - a power-ramp oscillator shape against 4 harmonics
  // (1/2/4/16), a narrow bandwidth for a clean-toned band. Its own
  // envelope sustains at full level (no decay-to-silence) rather than
  // "keyboard"'s own percussive one - a sustained "piano pad" character.
  static const PadSynthPresetParams kSawPiano = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::PowerRamp;
      o.base_shape_param = 0.3766f;
      o.harmonics = { { 1, 0.984f }, { 2, 0.922f }, { 4, 0.984f }, { 16, 0.563f } };
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 6.258f, 127, true },
    ImportedPositionParams{ 0, 0, 0, 0 },
    /* bandwidth_cents */ 1.3f, /* base_frequency_hz */ 261.6f, /* octaves */ 6, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ true);

  // "saw-piano-wide" - the same oscillator shape/harmonics 1/2/4 as
  // "saw-piano" (minus its own 16th harmonic), with a markedly wider
  // bandwidth (21.6 vs 1.3 cents), giving it a fatter/more-detuned
  // character from otherwise similar harmonic content.
  static const PadSynthPresetParams kSawPianoWide = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::PowerRamp;
      o.base_shape_param = 0.3766f;
      o.harmonics = { { 1, 0.984f }, { 2, 0.922f }, { 4, 0.984f } };
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 6.258f, 127, true },
    ImportedPositionParams{ 0, 0, 0, 0 },
    /* bandwidth_cents */ 21.6f, /* base_frequency_hz */ 261.6f, /* octaves */ 6, /* samples_per_octave */ 2,
    /* table_length */ 1 << 18, /* tuning_matched */ true);

  // "soft-pad" - a much steeper power-ramp oscillator shape than the
  // strings/piano family above, against just harmonic 1, plus a
  // spectrum-adjustment stage that reshapes even that single partial's
  // own overtone-free purity - "soft" spectrally, genuinely just one
  // reshaped partial. Profile autoscale is off for this preset
  // specifically (alpha fixed at 0.5), unlike every other preset here.
  static const PadSynthPresetParams kSoftPad = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::PowerRamp;
      o.base_shape_param = 2.871f;
      o.harmonics = { { 1, 0.984f } };
      o.spectrum_adjust_kind = ImportedSpectrumAdjustKind::PowerLaw;
      o.spectrum_adjust_gamma = 2.936f;
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 6.258f, 127, /* autoscale */ false },
    ImportedPositionParams{ 0, 0, 0, 0 },
    /* bandwidth_cents */ 25.6f, /* base_frequency_hz */ 261.6f, /* octaves */ 3, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ true);

  // GM's Synth Brass programs are the deliberately-synthesized brass
  // slots, unlike brass.trumpet/brass.section's own real acoustic
  // instruments (see InstrumentLibrary.cpp's own brass.synth/
  // brass.synth.soft comment) - a bright, fairly rich harmonic series
  // (rolloff near 1.0) for the punchy "sawtooth brass" character analog
  // synth brass patches are built from, no vowel formants (a brass stab
  // has no vocal-tract-style resonance to model). A little harmonic
  // jitter, same reasoning as the choir presets - a real analog synth's
  // own oscillators are never perfectly clean either.
  static const PadSynthPresetParams kSynthBrass{
    /* bandwidth_cents            */ 16.0f,
    /* bandwidth_scale_exponent   */ 0.6f,
    /* partial_count              */ 40,
    /* amplitude_rolloff_exponent */ 1.0f,
    /* formants                   */ {},
    /* harmonic_amplitude_jitter  */ 0.05f,
  };

  // "choir-aah" is an explicit, discoverable name for the same F1/F2/F3
  // shape kFormantVocal already uses, tuned toward an open "ah" vowel -
  // kept as a distinct alias (not a rename) so "formant-vocal" keeps
  // working for any song/preset reference already using it.
  //
  // "choir-ooh" is a genuinely different vowel, not a copy with new
  // numbers: real acoustic "oo" (as in "boot") has its first two formants
  // both low and close together (F1~300Hz, F2~870Hz - versus "ah"'s
  // 700/1220), which is what actually gives it that dark, rounded,
  // "hooting" quality rather than an open one. The base spectrum is
  // darkened to match: a steeper rolloff (2.2 vs "ah"'s 1.8) and a
  // weaker/narrower F3 (an "oo"'s third formant is real but comparatively
  // quiet - gain 2.0 vs "ah"'s 3.0). The singer's formant here is
  // deliberately weaker than "ah"'s own - a closed, rounded "oo" is a
  // genuinely darker vowel than an open "ah" even in a trained voice, and
  // a full-strength ring would fight that.
  static const PadSynthPresetParams kChoirOoh{
    /* bandwidth_cents            */ 20.0f,
    /* bandwidth_scale_exponent   */ 0.6f,
    /* partial_count              */ 48,
    /* amplitude_rolloff_exponent */ 2.2f,
    /* formants                   */ {
      { /* center_hz */ 300.0f,  /* bandwidth_hz */ 100.0f, /* gain */ 6.0f },  // F1
      { /* center_hz */ 870.0f,  /* bandwidth_hz */ 140.0f, /* gain */ 4.0f },  // F2
      { /* center_hz */ 2240.0f, /* bandwidth_hz */ 200.0f, /* gain */ 2.0f },  // F3
      { /* center_hz */ 2900.0f, /* bandwidth_hz */ 350.0f, /* gain */ 2.5f },  // singer's formant (F3-F5 cluster), weaker
    },
    /* harmonic_amplitude_jitter  */ 0.15f,
  };

  // "choir-pad4" - a warped-half-sine oscillator shape with its own time
  // warp (k1=0.1671, phi=0.5039, k3=10), an exponential-lowpass filter,
  // and a harmonic shift of 7. Used at pad.choir/lead.voice.
  static const PadSynthPresetParams kChoirPad4 = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::WarpedHalfSine;
      o.base_shape_param = 0.943f;
      o.base_warp = ImportedTimeWarp{ true, 0.1671f, 0.5039f, 10.0f };
      o.harmonics = { { 1, 0.984f } };
      o.filter = ImportedFilterParams{ ImportedFilterKind::ExponentialLowpass, 0.994256f, 0.00055f, 0, 1.0f };
      o.harmonic_shift = 7;
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 21.72f, 127, true },
    ImportedPositionParams{ 0, 0, 0, 0 },
    /* bandwidth_cents */ 63.7f, /* base_frequency_hz */ 261.6f, /* octaves */ 4, /* samples_per_octave */ 3,
    /* table_length */ 1 << 17, /* tuning_matched */ true,
    /* envelope_anchor_hz */ 289.4f, /* envelope_tracking */ 0.782f);

  // "choir-pad4-ooh" - "choir-pad4" darkened toward a closed, rounded "oo"
  // vowel: the same oscillator shape, time warp, filter, and harmonic
  // shift, plus a spectrum-adjustment stage that rolls off the upper
  // harmonics (the same darkening amount "long-spacechoir2" - a sibling
  // in this same warped-half-sine family - already uses for its own
  // spectrum adjustment).
  static const PadSynthPresetParams kChoirPad4Ooh = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::WarpedHalfSine;
      o.base_shape_param = 0.943f;
      o.base_warp = ImportedTimeWarp{ true, 0.1671f, 0.5039f, 10.0f };
      o.harmonics = { { 1, 0.984f } };
      o.filter = ImportedFilterParams{ ImportedFilterKind::ExponentialLowpass, 0.994256f, 0.00055f, 0, 1.0f };
      o.spectrum_adjust_kind = ImportedSpectrumAdjustKind::PowerLaw;
      o.spectrum_adjust_gamma = 0.6427f;
      o.harmonic_shift = 7;
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 21.72f, 127, true },
    ImportedPositionParams{ 0, 0, 0, 0 },
    /* bandwidth_cents */ 63.7f, /* base_frequency_hz */ 261.6f, /* octaves */ 4, /* samples_per_octave */ 3,
    /* table_length */ 1 << 17, /* tuning_matched */ true,
    /* envelope_anchor_hz */ 289.4f, /* envelope_tracking */ 0.782f);

  // "long-spacechoir2" - the same warped-half-sine/time-warp/lowpass-
  // filter/shift-of-7 family as "choir-pad4", with its own distinct warp
  // constants (k1=0.1599, phi=0.5118, k3=13) and an added spectrum-
  // adjustment stage (gamma=0.6427). Used at pad.halo, wrapped in
  // <phaser> for its own slow, shimmering motion.
  static const PadSynthPresetParams kLongSpaceChoir2 = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::WarpedHalfSine;
      o.base_shape_param = 0.943f;
      o.base_warp = ImportedTimeWarp{ true, 0.1599f, 0.5118f, 13.0f };
      o.harmonics = { { 1, 0.984f } };
      o.filter = ImportedFilterParams{ ImportedFilterKind::ExponentialLowpass, 0.994256f, 0.00055f, 0, 1.0f };
      o.spectrum_adjust_kind = ImportedSpectrumAdjustKind::PowerLaw;
      o.spectrum_adjust_gamma = 0.6427f;
      o.harmonic_shift = 7;
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 21.72f, 127, true },
    ImportedPositionParams{ 0, 0, 0, 0 },
    /* bandwidth_cents */ 64.9f, /* base_frequency_hz */ 261.6f, /* octaves */ 4, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ true,
    /* envelope_anchor_hz */ 126.5f, /* envelope_tracking */ 0.782f,
    /* postprocess_kind */ PadSynthPostprocessKind::ResidueClassWeighting,
    /* postprocess_n */ 2, /* postprocess_r */ 0, /* postprocess_amount */ 0.646f);

  // "bells-3" - a Gaussian-pulse oscillator shape against just harmonic
  // 1, reshaped by an arctangent waveshaper and a single-harmonic boost
  // filter (harmonic 1 x1.946) - the same shaping family as
  // "synth-piano-3-b". Partial positions land at 1,3,5,8,12,16,21,27,...
  static const PadSynthPresetParams kBells3 = importedPreset(
    []{
      OCP o;
      o.base_function = ImportedBaseFunction::GaussianPulse;
      o.base_shape_param = 0.5508f;
      o.harmonics = { { 1, 0.984f } };
      o.waveshaper_kind = ImportedWaveshaperKind::Arctangent;
      o.waveshaper_k = 6.247f;
      o.filter = ImportedFilterParams{ ImportedFilterKind::SingleHarmonicBoost, 0.0f, 0.0f, 1, 1.946f };
      return o;
    }(),
    ImportedProfileParams{ ImportedProfileType::Gaussian, 6.258f, 127, true },
    ImportedPositionParams{ 6, 255, 107, 255 },
    /* bandwidth_cents */ 15.9f, /* base_frequency_hz */ 392.4f, /* octaves */ 5, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ true,
    /* envelope_anchor_hz */ 392.9f, /* envelope_tracking */ 0.634f,
    /* postprocess_kind */ PadSynthPostprocessKind::ResidueClassWeighting,
    /* postprocess_n */ 4, /* postprocess_r */ 0, /* postprocess_amount */ 0.646f);

  if (name == "formant-vocal") return kFormantVocal;
  if (name == "choir-aah") return kFormantVocal;
  if (name == "choir-ooh") return kChoirOoh;
  if (name == "choir-pad4") return kChoirPad4;
  if (name == "choir-pad4-ooh") return kChoirPad4Ooh;
  if (name == "long-spacechoir2") return kLongSpaceChoir2;
  if (name == "glass") return kGlass;
  if (name == "mellotron") return kMellotron;
  if (name == "keyboard") return kKeyboard;
  if (name == "synth-piano-3-b") return kSynthPiano3B;
  if (name == "church-organ") return kChurchOrgan;
  if (name == "bells") return kBells;
  if (name == "bells-3") return kBells3;
  if (name == "strings") return kStrings;
  if (name == "dual-strings") return kDualStrings;
  if (name == "saw-piano") return kSawPiano;
  if (name == "saw-piano-wide") return kSawPianoWide;
  if (name == "soft-pad") return kSoftPad;
  if (name == "synth-brass") return kSynthBrass;
  return kWarm;
}

#endif
