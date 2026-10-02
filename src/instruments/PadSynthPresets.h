#ifndef _PADSYNTHPRESETS_H_
#define _PADSYNTHPRESETS_H_

#include "PadSynthTable.h"

#include <string>

// The numeric parameters behind each preset below come from Paul Nasca's
// PADsynth algorithm description (https://zynaddsubfx.sourceforge.io/doc/
// PADsynth/PADsynth.htm) and its example parameter files.

// Short local aliases - only used to keep the preset data below readable;
// never exposed outside this file.
using OCP = OscillatorShaping::OscillatorChainParams;
using OscBaseFunction = OscillatorShaping::BaseFunction;
using OscWaveshaperKind = OscillatorShaping::WaveshaperKind;
using OscFilterKind = OscillatorShaping::FilterKind;
using OscFilterParams = OscillatorShaping::HarmonicFilterParams;
using OscTimeWarp = OscillatorShaping::TimeWarp;
using OscSpectrumAdjustKind = OscillatorShaping::SpectrumAdjustKind;
using ProfileParams = PadSynthProfile::ProfileParams;
using ProfileType = PadSynthProfile::ProfileType;
using PositionParams = PadSynthProfile::PositionParams;
using PositionType = PadSynthProfile::PositionType;

// Builds a PadSynthParams - see PadSynth::loadParameters() for how
// tuning_matched/envelope_*/postprocess_* still act as this preset's own
// default for the matching XML attribute (an explicit attribute always
// overrides it); edo_steps/seed are filled in per-instance at table-build
// time, not here.
inline PadSynthParams preset(OCP oscillator, ProfileParams profile, PositionParams position,
                                      float bandwidth_cents, float base_frequency_hz, int octaves, int samples_per_octave,
                                      int table_length, bool tuning_matched,
                                      float envelope_anchor_hz = 0.0f, float envelope_tracking = 0.0f,
                                      SpectralPostprocessKind postprocess_kind = SpectralPostprocessKind::None,
                                      int postprocess_n = 0, int postprocess_r = 0, float postprocess_amount = 0.0f) {
  PadSynthParams params;
  params.oscillator = std::move(oscillator);
  params.profile = profile;
  params.position = position;
  params.bandwidth_cents = bandwidth_cents;
  params.base_frequency_hz = base_frequency_hz;
  params.octaves = octaves;
  params.samples_per_octave = samples_per_octave;
  params.table_length = table_length;
  params.tuning_matched = tuning_matched;
  params.envelope_anchor_hz = envelope_anchor_hz;
  params.envelope_tracking = envelope_tracking;
  params.postprocess_kind = postprocess_kind;
  params.postprocess_n = postprocess_n;
  params.postprocess_r = postprocess_r;
  params.postprocess_amount = postprocess_amount;
  return params;
}

// An unrecognized preset name falls back to "strings" rather than
// asserting - the same "unrecognized name falls back to a real default"
// shape TapeDegradationPresets.h's own getTapeDegradationPreset() already
// uses.
inline const PadSynthParams & getPadSynthPreset(const std::string & name) {
  // "keyboard" - a sparse, few-harmonic piano tone: a power-ramp
  // oscillator shape, an arctangent waveshaper, an oscillator time warp,
  // and fractional-stretch partial positions (partial 10 lands at 10.04).
  // Tuning matching stays off - turning it on would erase the stretch
  // that gives the low partials their piano-string character. Used as a
  // PadSynth-side comparison point against the additive piano
  // (docs/additive.md): PadSynth has no per-partial decay of its own, so
  // this preset's "struck" quality comes entirely from the wrapping
  // `<envelope>`'s own decay-to-zero-sustain shape.
  static const PadSynthParams kKeyboard = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::PowerRamp;
      o.base_shape_param = 2.871f;
      o.harmonics = { { 1, 0.984f }, { 2, 0.984f }, { 5, 0.547f }, { 6, 0.625f } };
      o.waveshaper_kind = OscWaveshaperKind::Arctangent;
      o.waveshaper_k = 46.72f;
      o.oscillator_warp = OscTimeWarp{ true, 0.1053f, -0.0039f, 1.0f };
      return o;
    }(),
    ProfileParams{ ProfileType::Rectangular, 6.258f, true },
    PositionParams{ PositionType::Stretch, 0.00827269256f, 0.219607845f, 1.0f },
    /* bandwidth_cents */ 11.5f, /* base_frequency_hz */ 392.4f, /* octaves */ 5, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ false,
    /* envelope_anchor_hz */ 115.6f, /* envelope_tracking */ 0.980f);

  // "synth-piano-3-b" - a sibling of "keyboard": a Gaussian-pulse
  // oscillator shape, an arctangent-plus-single-harmonic-boost filter
  // chain (the same shaping family as "bells-3"), a slightly stronger
  // partial-10 stretch (10.06 vs "keyboard"'s 10.04), and its own
  // stretch-mix postprocess.
  static const PadSynthParams kSynthPiano3B = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::GaussianPulse;
      o.base_shape_param = 0.5273f;
      o.harmonics = { { 1, 0.984f } };
      o.waveshaper_kind = OscWaveshaperKind::Arctangent;
      o.waveshaper_k = 6.247f;
      o.filter = OscFilterParams{ OscFilterKind::SingleHarmonicBoost, 0.0f, 0.0f, 1, 1.946f };
      return o;
    }(),
    ProfileParams{ ProfileType::Gaussian, 6.258f, true },
    PositionParams{ PositionType::Stretch, 0.0120879561f, 0.219607845f, 1.0f },
    /* bandwidth_cents */ 32.5f, /* base_frequency_hz */ 392.4f, /* octaves */ 6, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ false,
    /* envelope_anchor_hz */ 233.2f, /* envelope_tracking */ 0.634f,
    /* postprocess_kind */ SpectralPostprocessKind::StretchMix, /* postprocess_n */ 2, /* postprocess_r */ 0, /* postprocess_amount */ 0.646f);

  // "church-organ" - a pipe organ's flue pipes are about as close to
  // PADsynth's own idealized case as a real instrument gets: a steady,
  // essentially beat-free harmonic stack with no per-note decay of its
  // own. A clipped-triangle oscillator shape against 8 harmonics, plus an
  // exponential-lowpass harmonic filter, gives this preset its drawbar-
  // organ-style spectrum. Used as a fallback at organ.pipe (a real
  // acoustic organ from a loaded instrument sample library always wins;
  // this only fills the leaf when none is available).
  static const PadSynthParams kChurchOrgan = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::ClippedTriangle;
      o.base_shape_param = 0.8477f;
      o.harmonics = { { 1, 0.984f }, { 2, 0.734f }, { 4, 0.500f }, { 8, 0.438f },
                       { 12, 0.156f }, { 16, 0.469f }, { 24, 0.188f }, { 32, 0.156f } };
      o.filter = OscFilterParams{ OscFilterKind::ExponentialLowpass, 0.977975f, 0.032346f, 0, 1.0f };
      return o;
    }(),
    ProfileParams{ ProfileType::Gaussian, 21.72f, true },
    PositionParams{},
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
  static const PadSynthParams kBells = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::None;
      o.harmonics = { { 1, 0.984f }, { 2, 0.172f }, { 4, 0.313f } };
      o.waveshaper_kind = OscWaveshaperKind::LogisticSigmoid;
      o.waveshaper_k = 8.443f;
      return o;
    }(),
    ProfileParams{ ProfileType::Gaussian, 6.258f, true },
    PositionParams{ PositionType::Stretch, 1.0f, 0.294117659f, 0.0f },
    /* bandwidth_cents */ 21.2f, /* base_frequency_hz */ 392.4f, /* octaves */ 5, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ true,
    /* envelope_anchor_hz */ 233.2f, /* envelope_tracking */ 0.634f,
    /* postprocess_kind */ SpectralPostprocessKind::ResidueClassWeighting,
    /* postprocess_n */ 2, /* postprocess_r */ 1, /* postprocess_amount */ 0.646f);

  // "strings" - a power-ramp oscillator shape against 2 harmonics. A very
  // simple, near-octave-doubled tone on its own - real ensemble character
  // comes from unison layering on top (InstrumentLibrary.cpp's own
  // string.synth.slow/pad.bowed registrations). Also the generic
  // fallback/"plain pad" tone (pad.warm, pad.sweep) where nothing more
  // specific applies.
  static const PadSynthParams kStrings = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::PowerRamp;
      o.base_shape_param = 0.3766f;
      o.harmonics = { { 1, 0.984f }, { 2, 0.828f } };
      return o;
    }(),
    ProfileParams{ ProfileType::Gaussian, 6.258f, true },
    PositionParams{},
    /* bandwidth_cents */ 57.3f, /* base_frequency_hz */ 261.6f, /* octaves */ 6, /* samples_per_octave */ 2,
    /* table_length */ 1 << 18, /* tuning_matched */ true);

  // "dual-strings" - the same power-ramp oscillator shape as "strings",
  // against just harmonic 1 and a wider bandwidth of its own.
  static const PadSynthParams kDualStrings = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::PowerRamp;
      o.base_shape_param = 0.3766f;
      o.harmonics = { { 1, 0.984f } };
      return o;
    }(),
    ProfileParams{ ProfileType::Gaussian, 6.258f, true },
    PositionParams{},
    /* bandwidth_cents */ 101.5f, /* base_frequency_hz */ 261.6f, /* octaves */ 6, /* samples_per_octave */ 2,
    /* table_length */ 1 << 18, /* tuning_matched */ true);

  // "saw-piano" - a power-ramp oscillator shape against 4 harmonics
  // (1/2/4/16), a narrow bandwidth for a clean-toned band. Its own
  // envelope sustains at full level (no decay-to-silence) rather than
  // "keyboard"'s own percussive one - a sustained "piano pad" character.
  static const PadSynthParams kSawPiano = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::PowerRamp;
      o.base_shape_param = 0.3766f;
      o.harmonics = { { 1, 0.984f }, { 2, 0.922f }, { 4, 0.984f }, { 16, 0.563f } };
      return o;
    }(),
    ProfileParams{ ProfileType::Gaussian, 6.258f, true },
    PositionParams{},
    /* bandwidth_cents */ 1.3f, /* base_frequency_hz */ 261.6f, /* octaves */ 6, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ true);

  // "saw-piano-wide" - the same oscillator shape/harmonics 1/2/4 as
  // "saw-piano" (minus its own 16th harmonic), with a markedly wider
  // bandwidth (21.6 vs 1.3 cents), giving it a fatter/more-detuned
  // character from otherwise similar harmonic content - the brighter,
  // more analog-synth-like end of that shared oscillator shape.
  static const PadSynthParams kSawPianoWide = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::PowerRamp;
      o.base_shape_param = 0.3766f;
      o.harmonics = { { 1, 0.984f }, { 2, 0.922f }, { 4, 0.984f } };
      return o;
    }(),
    ProfileParams{ ProfileType::Gaussian, 6.258f, true },
    PositionParams{},
    /* bandwidth_cents */ 21.6f, /* base_frequency_hz */ 261.6f, /* octaves */ 6, /* samples_per_octave */ 2,
    /* table_length */ 1 << 18, /* tuning_matched */ true);

  // "soft-pad" - a much steeper power-ramp oscillator shape than the
  // strings/piano family above, against just harmonic 1, plus a
  // spectrum-adjustment stage that reshapes even that single partial's
  // own overtone-free purity - "soft" spectrally, genuinely just one
  // reshaped partial. Profile autoscale is off for this preset
  // specifically (alpha fixed at 0.5), unlike every other preset here.
  static const PadSynthParams kSoftPad = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::PowerRamp;
      o.base_shape_param = 2.871f;
      o.harmonics = { { 1, 0.984f } };
      o.spectrum_adjust_kind = OscSpectrumAdjustKind::PowerLaw;
      o.spectrum_adjust_gamma = 2.936f;
      return o;
    }(),
    ProfileParams{ ProfileType::Gaussian, 6.258f, /* autoscale */ false },
    PositionParams{},
    /* bandwidth_cents */ 25.6f, /* base_frequency_hz */ 261.6f, /* octaves */ 3, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ true);

  // "choir-pad4" - a warped-half-sine oscillator shape with its own time
  // warp (k1=0.1671, phi=0.5039, k3=10), an exponential-lowpass filter,
  // and a harmonic shift of 7. Used at pad.choir/lead.voice.
  static const PadSynthParams kChoirPad4 = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::WarpedHalfSine;
      o.base_shape_param = 0.943f;
      o.base_warp = OscTimeWarp{ true, 0.1671f, 0.5039f, 10.0f };
      o.harmonics = { { 1, 0.984f } };
      o.filter = OscFilterParams{ OscFilterKind::ExponentialLowpass, 0.994256f, 0.00055f, 0, 1.0f };
      o.harmonic_shift = 7;
      return o;
    }(),
    ProfileParams{ ProfileType::Gaussian, 21.72f, true },
    PositionParams{},
    /* bandwidth_cents */ 63.7f, /* base_frequency_hz */ 261.6f, /* octaves */ 4, /* samples_per_octave */ 3,
    /* table_length */ 1 << 17, /* tuning_matched */ true,
    /* envelope_anchor_hz */ 289.4f, /* envelope_tracking */ 0.782f);

  // "long-spacechoir2" - the same warped-half-sine/time-warp/lowpass-
  // filter/shift-of-7 family as "choir-pad4", with its own distinct warp
  // constants (k1=0.1599, phi=0.5118, k3=13) and an added spectrum-
  // adjustment stage (gamma=0.6427). Used at pad.halo, wrapped in
  // <phaser> for its own slow, shimmering motion.
  static const PadSynthParams kLongSpaceChoir2 = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::WarpedHalfSine;
      o.base_shape_param = 0.943f;
      o.base_warp = OscTimeWarp{ true, 0.1599f, 0.5118f, 13.0f };
      o.harmonics = { { 1, 0.984f } };
      o.filter = OscFilterParams{ OscFilterKind::ExponentialLowpass, 0.994256f, 0.00055f, 0, 1.0f };
      o.spectrum_adjust_kind = OscSpectrumAdjustKind::PowerLaw;
      o.spectrum_adjust_gamma = 0.6427f;
      o.harmonic_shift = 7;
      return o;
    }(),
    ProfileParams{ ProfileType::Gaussian, 21.72f, true },
    PositionParams{},
    /* bandwidth_cents */ 64.9f, /* base_frequency_hz */ 261.6f, /* octaves */ 4, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ true,
    /* envelope_anchor_hz */ 126.5f, /* envelope_tracking */ 0.782f,
    /* postprocess_kind */ SpectralPostprocessKind::ResidueClassWeighting,
    /* postprocess_n */ 2, /* postprocess_r */ 0, /* postprocess_amount */ 0.646f);

  // "bells-3" - a Gaussian-pulse oscillator shape against just harmonic
  // 1, reshaped by an arctangent waveshaper and a single-harmonic boost
  // filter (harmonic 1 x1.946) - the same shaping family as
  // "synth-piano-3-b". Partial positions land at 1,3,5,8,12,16,21,27,...
  static const PadSynthParams kBells3 = preset(
    []{
      OCP o;
      o.base_function = OscBaseFunction::GaussianPulse;
      o.base_shape_param = 0.5508f;
      o.harmonics = { { 1, 0.984f } };
      o.waveshaper_kind = OscWaveshaperKind::Arctangent;
      o.waveshaper_k = 6.247f;
      o.filter = OscFilterParams{ OscFilterKind::SingleHarmonicBoost, 0.0f, 0.0f, 1, 1.946f };
      return o;
    }(),
    ProfileParams{ ProfileType::Gaussian, 6.258f, true },
    PositionParams{ PositionType::Stretch, 1.0f, 0.419607848f, 0.0f },
    /* bandwidth_cents */ 15.9f, /* base_frequency_hz */ 392.4f, /* octaves */ 5, /* samples_per_octave */ 2,
    /* table_length */ 1 << 17, /* tuning_matched */ true,
    /* envelope_anchor_hz */ 392.9f, /* envelope_tracking */ 0.634f,
    /* postprocess_kind */ SpectralPostprocessKind::ResidueClassWeighting,
    /* postprocess_n */ 4, /* postprocess_r */ 0, /* postprocess_amount */ 0.646f);

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
  if (name == "choir-pad4") return kChoirPad4;
  if (name == "long-spacechoir2") return kLongSpaceChoir2;
  return kStrings;
}

#endif
