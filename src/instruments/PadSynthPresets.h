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
// amplitude_rolloff_exponent, formants and harmonic_amplitude_jitter are
// this codebase's own extension beyond the bare bandwidth/bandwidthScale/
// partials PADsynth parameters (see PadSynthWavetable.h's own doc comment
// on amplitude_rolloff_exponent/PadSynthFormant/harmonic_amplitude_jitter)
// - without them, every preset would only be able to differ by bandwidth
// and partial count, which can't tell a clean "glass" bell apart from a
// "formant-vocal" character the way a real per-harmonic amplitude shape
// can, and can't add the irregular, non-smooth per-harmonic detail real
// PADsynth's own oscillator-driven harmonic arrays have either.
struct PadSynthPresetParams {
  float bandwidth_cents;
  float bandwidth_scale_exponent;
  int partial_count;
  float amplitude_rolloff_exponent;
  std::vector<PadSynthFormant> formants;
  float harmonic_amplitude_jitter = 0.0f;
  // Non-empty replaces amplitude_rolloff_exponent's formula outright (see
  // PadSynthWavetable.h's own doc comment) - real, explicit per-harmonic
  // data ported from a real PADsynth patch, index 0 = harmonic 1.
  std::vector<float> harmonic_amplitudes = {};

  // Preset-level default for the "tuningMatched" XML attribute - an
  // explicit attribute still always overrides this (see PadSynth::
  // loadParameters()) but a preset ported from real patch data supplies
  // its own known-correct default rather than every preset silently
  // inheriting the engine's own hardcoded `true`.
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

  // Non-null selects ImportedPadSynthTable (the clean-room, ZynAddSubFX-
  // faithful oscillator-chain + profile-placement renderer, docs/
  // padsynth.md) over PadSynthWavetable's own from-scratch Gaussian-band
  // one for this preset entirely - every field above except
  // tuning_matched/envelope_*/postprocess_* (still read the same way by
  // both renderers) is then unused. Every real ZynAddSubFX-imported
  // preset with a fully or mostly portable oscillator chain uses this;
  // this codebase's own invented presets (warm/glass/formant-vocal/
  // choir-ooh/synth-brass/mellotron) do not.
  std::shared_ptr<ImportedPadSynthParams> imported;
};

// Short local aliases - only used to keep the imported-preset data tables
// below (ported directly from ZynAddSubFX's own public algorithm
// description, docs/padsynth.md) readable; never exposed outside this file.
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

// Builds a PadSynthPresetParams for an imported (ZynAddSubFX-sourced)
// preset that uses ImportedPadSynthTable's own oscillator-chain +
// profile-placement rendering (docs/padsynth.md) - every field of the
// outer PadSynthPresetParams this codebase's own Gaussian-band renderer
// would otherwise read is irrelevant here except tuning_matched/
// envelope_*/postprocess_*, which both renderers read the same way (an
// explicit XML attribute still always overrides these).
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
  // The sensible middle-ground default. Steepening the rolloff (1/n, a
  // bright sawtooth spectrum, to 1/n^2) fixed the earlier "buzzy, high
  // frequencies, doesn't sound like a pad" report, but a second listen
  // found it still didn't read as "warm" - the demo's own narrow-
  // bandwidth A/B comparison (bandwidth=8, i.e. this preset's own
  // bandwidth_cents overridden down near Glass's own value) sounded
  // warmer than the unmodified preset. That points at bandwidth itself,
  // not just rolloff: even with quiet high harmonics, a wide Gaussian
  // band spreads and beats *every* partial including the loud low ones,
  // and that spread/beating itself reads as bright/synthetic rather than
  // warm - "warm" wants each low partial closer to a clean tone. Narrowed
  // bandwidth_cents from 40 to 18 (between Glass's 8 and the original 40)
  // for exactly that.
  static const PadSynthPresetParams kWarm{
    /* bandwidth_cents            */ 18.0f,
    /* bandwidth_scale_exponent   */ 0.6f,
    /* partial_count              */ 32,
    /* amplitude_rolloff_exponent */ 2.0f,
    /* formants                   */ {},
  };

  // Ported data, not a from-scratch approximation (the earlier version of
  // this preset was invented before the uploaded ZynAddSubFX example data
  // was checked, and is dropped now that the real patch is available):
  // the uploaded factory patch "Synth Piano 3" (synth_piano.xmz/
  // noefx_synth_piano.xmz) has an explicit, sparse <HARMONICS> list - only
  // harmonics 1/2/5/6 carry real energy, everything else silent - and a
  // percussive amplitude envelope (instant attack, long decay all the way
  // to S_val=0, no sustain plateau at all) rather than a held tone. Same
  // porting boundary as kChurchOrgan/kBells: real numeric data, never the
  // GPL application code that reads/renders it. Used as a PadSynth-side
  // comparison point against the additive piano (docs/additive.md's
  // struck-string-based one): PadSynth has no per-partial decay of its
  // own, so this preset's own "struck" quality has to come entirely from
  // the wrapping `<envelope>`'s own decay-to-zero-sustain shape (see
  // InstrumentLibrary.cpp's/songs/oscillator_demo.xml's own "PadSynth
  // Keyboard" envelope) rather than any true time-varying spectral
  // evolution - a real and audible difference from additive's true
  // per-partial decay worth hearing side by side. bandwidth_cents is
  // still an approximation (the real patch's own raw "bandwidth" units,
  // 450, have no confirmed conversion to cents - see docs/padsynth.md);
  // kept narrow, for clarity/presence on a small, sparse harmonic set.
  // The real patch's own filter+filter-envelope isn't ported, same reason
  // as kChurchOrgan.
  // "keyboard" is Synth Piano 3 (A) - now the real full oscillator chain
  // (power-ramp base waveform e=2.871, explicit harmonics 1/2/5/6,
  // arctangent waveshaper, a real oscillator time warp) plus real PADsynth
  // rendering data (rectangular profile, type-6 fractional-stretch partial
  // positions landing partial 10 at 10.04), clean-room-implemented from
  // ZynAddSubFX's own public algorithm description (see docs/padsynth.md;
  // ImportedOscillatorChain.h/ImportedPadSynthTable.h for the renderer).
  // Tuning matching stays explicitly OFF - turning it on would erase the
  // very stretch that makes this preset's own low partials read as a real
  // piano string's rather than a plain harmonic tone.
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

  // Synth Piano 3 (B) - the same real patch's second documented instance:
  // a Gaussian-pulse base waveform instead of (A)'s power ramp, the same
  // arctangent-plus-single-harmonic-boost shaping Bells 3 uses, a slightly
  // stronger partial-10 stretch (10.06 vs (A)'s 10.04), and its own
  // distinct remap/postprocess (stretch-mix, which (A) doesn't have).
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

  // A handful of vowel-like formant resonances (loosely modeled on an "ah"
  // vowel's own F1/F2/F3) boost specific harmonic bands regardless of
  // fundamental, the way a vocal tract's own fixed resonant cavities do.
  //
  // Two rounds of retuning so far. Round 1 (gain 1.4-3x, 100-250Hz-wide
  // bumps, a mild 1.3 rolloff) read as "synth string," not vocal - the
  // boosts were too gentle to stand out. Round 2 overcorrected: very
  // narrow base partials (15 cents) plus very narrow, very strong formant
  // bumps (50-100Hz wide, +16-20dB) read as "metallic," not choir. The
  // actual problem with round 2: a real vocal formant isn't a spike on
  // one partial, it's a broad resonance boosting a *cluster* of several
  // neighboring harmonics together - at a single narrow band's typical
  // fundamental (e.g. middle C, harmonics ~260Hz apart), a 50-100Hz-wide
  // formant mostly only reaches ONE nearby harmonic, so a strong gain
  // there just spotlights a single overtone (bell/metallic), not a
  // resonant region. Fixed by widening the formant bumps back out (120-
  // 220Hz, wide enough to span several neighboring harmonics into a
  // genuine cluster) while moderating their gain (4-6x, not 7-10x, since
  // several boosted harmonics together read as loud even at a lower
  // per-partial gain) and loosening the base spectrum back toward a more
  // natural richness (22 cents, 1.8 rolloff - between round 1's too-mild
  // 1.3 and round 2's too-sparse 2.2) so there's enough underlying
  // harmonic density for a formant to actually have several partials to
  // cluster, rather than reading as glassy/discrete on its own.
  //
  // Round 3: F1/F2/F3 alone (three smooth Gaussian bumps) still read as
  // "synth string"/"metallic," not "human," even correctly widened and
  // balanced - because a smooth multi-bump curve is fundamentally the
  // wrong shape of thing to fix this with. Two real, specific pieces were
  // missing, not just wrong numbers:
  //  - A "singer's formant" - a well-documented acoustic feature of
  //    trained/choral voices (a real F3-F4-F5 cluster merging into one
  //    bright resonance around 2.8-3.4kHz, giving a voice its
  //    characteristic "ring"/cut-through-an-ensemble quality) - F3 alone,
  //    even boosted, sits below and reads as a generic upper-formant
  //    bump rather than that specific ring. Added as a fourth formant.
  //  - Irregular, non-smooth per-harmonic detail - what a real PADsynth
  //    implementation gets from an oscillator-driven per-harmonic array
  //    (ZynAddSubFX's own OSCIL/HARMONICS mechanism) and this codebase
  //    has no equivalent generator for; harmonic_amplitude_jitter is the
  //    fix (see PadSynthWavetable.h's own doc comment) - a small,
  //    deterministic amount of harmonic-to-harmonic unevenness that no
  //    smooth rolloff-plus-formants curve can produce on its own, which
  //    is exactly the kind of texture that tells "a real resonant body"
  //    apart from "a clean synthesized curve."
  // Round 4 (since reverted): a "still metallic" report after the
  // singer's-formant/jitter addition above was initially met by widening
  // the base bandwidth further (22 to 28 cents) and pulling the singer's-
  // formant gain back (4.0 to 2.5), on the theory that a formant boost on
  // a sparse comb of narrow, discrete partials spotlights one or two
  // overtones rather than reading as a resonant cluster. Reverted at the
  // user's own request, to keep this first version's parameters matching
  // what the demo song was actually built and shared against, rather than
  // an unheard, unvalidated guess on top of it - round 3's own numbers
  // below (22 cents, gain 4.0) are what ships; a further metallic-focused
  // retuning pass is a decision for a later round, informed by an actual
  // listen, not made unilaterally again here.
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

  // "bowed-ensemble" (a from-scratch, formula-only approximation) is
  // dropped entirely, per explicit request, now that the real ZynAddSubFX
  // "Strings" patch is available (kStrings below) - see
  // InstrumentLibrary.cpp's own pad.bowed/string.synth.slow registrations
  // and this file's own kMellotron for what replaced it.

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

  // A Mellotron's "strings" tape was 3 real violins recorded per note - this
  // preset's own job is purely spectral character (what a massed bowed
  // string ensemble sounds like), not literal unison/detune voices; that
  // comes from <tapeDegradation preset="mellotron"> layered on top instead
  // (built separately - see docs/tape_degradation.md). Now uses the same
  // real ported harmonic array as "strings" (kStrings below - the real
  // ZynAddSubFX "Strings" patch, fully portable, adaptive_harmonics=0)
  // rather than the earlier invented smooth rolloff dropped alongside
  // "bowed-ensemble" - a Mellotron strings tape and this preset's own
  // "strings" are literally the same real-world instrument family, so
  // there's no reason for them to rest on different (and, before, both
  // invented) spectral data. Bandwidth/rolloff stay this preset's own
  // slightly narrower/steeper values, reading a little more "recorded
  // tape" than a live "strings" pad.
  static const PadSynthPresetParams kMellotron{
    /* bandwidth_cents            */ 8.0f,
    /* bandwidth_scale_exponent   */ 0.45f,
    /* partial_count              */ 2,
    /* amplitude_rolloff_exponent */ 1.0f, // unused - harmonic_amplitudes below replaces it
    /* formants                   */ {},
    /* harmonic_amplitude_jitter  */ 0.0f,
    /* harmonic_amplitudes        */ { 127.0f/127.0f, 117.0f/127.0f },
  };

  // A pipe organ's flue pipes are about as close to PADsynth's own
  // idealized case as a real instrument gets - a steady, essentially
  // beat-free harmonic stack with no per-note decay of its own (the
  // "church-organ" - ports real data (dropping the earlier from-scratch,
  // formula-only "organ-pipe" preset entirely, per explicit request - a
  // from-scratch approximation isn't worth keeping once the real patch
  // it was standing in for is actually available): the uploaded ZynAddSubFX factory
  // patch "Church Organ 3" (noefx_organ_choir.xmz, PART id="1") has an
  // explicit, sparse <HARMONICS> list - only harmonics 1/2/4/8/12/16/24/32
  // carry real energy (mag/127, everything else silent) - a genuine
  // drawbar-organ-style spectrum a smooth 1/n^rolloff curve structurally
  // cannot produce, which is exactly why kOrganPipe's own formula-only
  // approximation read as "quite bad": it never had this sparse structure
  // at all. Ported as data (the numeric harmonic magnitudes themselves),
  // not the GPL application code that reads/renders them - the same
  // "public description plus real parameter data, never the app source"
  // boundary this file's own choir-vs-ZynAddSubFX research already
  // established. bandwidth_cents is still an approximation (the real
  // patch's own raw "bandwidth" units, 365, have no confirmed/documented
  // conversion to cents outside ZynAddSubFX's own GPL source, which stays
  // off-limits - see docs/padsynth.md) - kept narrow, matching a real
  // organ pipe's own steady, minimally-beating tone. The real patch's own
  // filter (a 2-stage highpass with its own attack/decay/release
  // envelope, freq_track=127 for full keyboard tracking) also isn't
  // ported - this codebase's <biquadFilter> has no filter-envelope
  // modulation mechanism yet (its own `envelope_` member is unused -
  // BiquadFilterDsp never reads it to modulate fc), a separate, larger gap
  // than this preset alone.
  // Now the real full oscillator chain: a clipped-triangle base waveform
  // (a'=0.8477) whose harmonic expansion, against the same 8 explicit
  // partials, plus an exponential-lowpass harmonic filter, reproduces this
  // patch's own real drawbar-organ-style spectrum - not just the bare
  // harmonic-magnitude list the earlier version used. bandwidth_cents is
  // now the real converted figure (5.2), not an approximation.
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

  // "bells" - the uploaded ZynAddSubFX factory patch "Bells"
  // (noefx_bell_strings.xmz) has just 3 explicit harmonics (1/2/4 - mag
  // 127/75/84), everything else silent - the actual reason a real
  // bell/chime reads as a bell at all: a small handful of dominant
  // partials, not a dense harmonic series. tuningMatched=false (an
  // explicit override, set in InstrumentLibrary.cpp's own bells
  // registration, matching pad.metallic's identical "false" reasoning)
  // is still what supplies pad.metallic's own dissonant/bell-like
  // character on top of this - exact-integer harmonics read as
  // "out of tune" against this engine's own scale-quantized tuning
  // system the same way a real bell's non-integer partials read as
  // dissonant against a fixed pitch. Same porting caveats as
  // kChurchOrgan above - real data, not GPL application logic; the real
  // patch's own filter/filter-envelope isn't ported for the same reason.
  // Caveat worth flagging: the real patch's own `adaptive_harmonics` is 2
  // (nonzero), meaning ZynAddSubFX's own internal harmonic generator
  // contributes to the real spectrum beyond just these 3 explicit
  // entries - that generator is GPL application logic, not exposed
  // parameter data, so it stays off-limits (see docs/padsynth.md). This
  // preset treats the 3 explicit harmonics as the *complete* spectrum,
  // which is the most this codebase can faithfully claim from the data
  // alone - a reasonable approximation, not a byte-for-byte match to
  // what the real patch actually sounds like.
  //
  // Now the real full oscillator chain: base_function "none" places the 3
  // explicit harmonics (1/2/4) directly, then a logistic-sigmoid
  // waveshaper reshapes that sparse set nonlinearly - genuinely producing
  // the rest of this patch's real spectrum (the earlier version's bare
  // 3-harmonic array, with nothing past harmonic 4, undersold how rich the
  // real patch actually is; the waveshaper is exactly what fills that in).
  // Partial positions are the explicit type-6 stretch formula (P1=255,
  // P2=75, P3=255 - integer positions, landing at 1,2,4,5,7,9,11,13,...),
  // not a plain harmonic series - a real bell/chime's own strike-tone
  // partials aren't simple integer multiples. bandwidth_cents/remap are
  // the real converted figures. Any song that still wants Bells' own bare,
  // untempered dissonance (e.g. a deliberately bell-like/out-of-tune
  // character) sets tuningMatched="false" explicitly, same as any other
  // preset's default - an explicit XML attribute always overrides a
  // preset's own default.
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

  // "strings" - the real "Strings" patch (noefx_bell_strings.xmz, PART
  // id="1") - unlike Bells above, its own adaptive_harmonics is 0, so its
  // 2 explicit harmonics (1/2, mag 127/117) genuinely are the complete
  // spectrum, not a partial view of something a GPL-internal generator
  // also shapes. A very simple, near-octave-doubled tone on its own -
  // the real "ensemble" character comes from unison layering on top (see
  // InstrumentLibrary.cpp's own string.synth.slow registration), same
  // reasoning as this codebase's own earlier bowed-ensemble+<multiply>
  // choice.
  // Now the real full oscillator chain: a power-ramp base waveform
  // (e=0.3766) against the 2 explicit harmonics - real, converted
  // bandwidth (57.3 cents, notably wider than the earlier approximated
  // 10.0, giving this preset a real beating/chorusing character on its
  // own even before the <multiply> layering InstrumentLibrary.cpp adds).
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

  // "dual-strings" - the real "Dual Strings Oct2" patch: same power-ramp
  // base waveform as "strings" (a different real patch, not a duplicate -
  // its own e is 0.3766 here, distinct from "strings" only in harmonic
  // content/bandwidth), genuinely just harmonic 1. Several instances of
  // this same named patch appear across the example set at different
  // bandwidths (11.5-101.5 cents observed, table lengths 2^17-2^18); the
  // "0km, part 0" instance (101.5 cents, 2^18) is used here as the single
  // representative one exposed under this preset name. Its own "dual"/
  // octave-doubled character comes from unison layering on top, same as
  // "strings" - see InstrumentLibrary.cpp's own pad.bowed/PadSynth Dual
  // Strings <multiply> wrapping.
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

  // "saw-piano" - the real "Saw Piano 1" patch: power-ramp base waveform
  // (e=0.3766) against 4 explicit harmonics (1/2/4/16). Notably its own
  // amplitude envelope sustains at full level (no decay-to-silence)
  // rather than Synth Piano 3's own percussive one - a sustained "piano
  // pad" character, not a percussive one (see InstrumentLibrary.cpp's/the
  // demo song's own envelope for this preset). bandwidth_cents is the
  // real converted figure (1.3 - a very narrow, clean-toned band).
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

  // "saw-piano-wide" - the same base waveform/harmonics 1/2/4 as
  // "saw-piano" above, minus its own 16th harmonic - a distinct real
  // patch ("Saw Piano" in piano_saw_choir.xmz, not "Saw Piano 1"), with a
  // markedly wider real bandwidth (21.6 vs 1.3 cents), giving it a
  // fatter/more-detuned character from otherwise similar harmonic content.
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

  // "soft-pad" - the real "Soft Pad" patch: a much steeper power-ramp base
  // waveform (e=2.871) than the strings/piano family above, against just
  // harmonic 1, plus a real spectrum-adjustment stage (gamma=2.936) that
  // reshapes even that single partial's own overtone-free purity - "soft"
  // spectrally, not an approximation. Profile autoscale is off for this
  // preset specifically (alpha fixed at 0.5, per the real patch's own
  // data), unlike every other imported preset here.
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

  // GM's Synth Brass programs (62/63) are the deliberately-synthesized
  // brass slots, unlike brass.trumpet/brass.section's own real acoustic
  // instruments (see InstrumentLibrary.cpp's own brass.synth/
  // brass.synth.soft comment for why that distinction matters here) - a
  // bright, fairly rich harmonic series (rolloff near 1.0, closer to
  // Warm's own mellow 2.0 than Keyboard's 1.6) for the punchy "sawtooth
  // brass" character analog synth brass patches are built from, no vowel
  // formants (a brass stab has no vocal-tract-style resonance to model).
  // A little harmonic jitter, same reasoning as the choir presets - a
  // real analog synth's own oscillators are never perfectly clean either.
  static const PadSynthPresetParams kSynthBrass{
    /* bandwidth_cents            */ 16.0f,
    /* bandwidth_scale_exponent   */ 0.6f,
    /* partial_count              */ 40,
    /* amplitude_rolloff_exponent */ 1.0f,
    /* formants                   */ {},
    /* harmonic_amplitude_jitter  */ 0.05f,
  };

  // "choir-aah" is just an explicit, discoverable name for the same F1/F2/
  // F3 shape kFormantVocal already tuned toward an open "ah" vowel - kept
  // as a distinct alias (not a rename) so "formant-vocal" keeps working for
  // any song/preset reference already using it.
  //
  // "choir-ooh" is a genuinely different vowel, not a copy with new
  // numbers: real acoustic "oo" (as in "boot") has its first two formants
  // both low and close together (F1~300Hz, F2~870Hz - versus "ah"'s
  // 700/1220), which is what actually gives it that dark, rounded,
  // "hooting" quality rather than an open one. Reusing kFormantVocal's own
  // base spectrum unchanged would still read as "ah" underneath, so the
  // base is darkened to match: a steeper rolloff (2.2 vs 1.8, less
  // high-harmonic energy for a vocal tract shaped for a closed, rounded
  // vowel) and a weaker/narrower F3 (an "oo"'s third formant is real but
  // comparatively quiet - gain 2.0 vs "ah"'s 3.0 - so the mouth-cavity
  // darkness doesn't get undone by a bright top end).
  //
  // Same round-3 fixes as kFormantVocal above, in both cases for the same
  // reason: neither is about this vowel's own formant frequencies being
  // wrong, both are about what a smooth rolloff-plus-formant-bumps curve
  // structurally can't produce at all. The singer's formant here is
  // deliberately weaker than kFormantVocal's own - a closed, rounded "oo"
  // is a genuinely darker vowel than an open "ah" even in a trained
  // voice, and a full-strength ring would fight that.
  //
  // Round 4 (since reverted): same fix as kFormantVocal's own round 4
  // above, same reason, and reverted for the same reason too - see that
  // preset's own comment. Round 3's own numbers below (20 cents, gain
  // 2.5) are what ships.
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

  // "choir-pad4" - the real ZynAddSubFX "Choir Pad4" patch. Its own
  // explicit <HARMONICS> list has just harmonic 1 - but that's only a
  // limitation of the harmonic-*list*, not of what this codebase can
  // faithfully reproduce: the real spectrum comes from a warped-half-sine
  // base waveform (e=0.943) with its own real base time warp (k1=0.1671,
  // phi=0.5039, k3=10), an exponential-lowpass filter, and a harmonic
  // shift of 7 - all real, public-algorithm-derived oscillator parameters
  // (docs/padsynth.md), not ZynAddSubFX's own internal adaptive_harmonics
  // generator. This replaces the earlier version's own approximated base
  // spectrum (kFormantVocal's vowel formants standing in for the real
  // patch) entirely - the real oscillator chain now genuinely produces
  // this patch's own real harmonic content.
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

  // "long-spacechoir2" - the real "Long SpaceChoir2" patch: the same
  // warped-half-sine/base-warp/lowpass-filter/shift-of-7 family as Choir
  // Pad4 above, with its own distinct warp constants (k1=0.1599,
  // phi=0.5118, k3=13) and an added real spectrum-adjustment stage
  // (gamma=0.6427) - again the real oscillator chain, not an approximated
  // base spectrum.
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

  // "bells-3" - the real "Bells 3" patch: a gaussian-pulse base waveform
  // (a=0.5508) against just harmonic 1, reshaped by a real arctangent
  // waveshaper and a single-harmonic boost filter (harmonic 1 x1.946) -
  // genuinely producing this patch's own dense bell-like spectrum, not an
  // approximation borrowed from "bells". Partial positions are the
  // explicit type-6 stretch formula (P1=255, P2=107, P3=255 - integer
  // positions, landing at 1,3,5,8,12,16,21,27,...).
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
