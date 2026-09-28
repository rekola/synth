#ifndef _PADSYNTHPRESETS_H_
#define _PADSYNTHPRESETS_H_

#include "PadSynthWavetable.h"

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
};

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

  // A static, held-chord-friendly "keys"/electric-piano-like spectral
  // character - added specifically as a PadSynth-side comparison point
  // against the additive piano (docs/additive.md's struck-string-based
  // one): the same instrument-family idea (a keyboard note), but PadSynth
  // has no per-partial decay of its own - its whole "struck" quality has
  // to come from the wrapping <envelope>'s own decay stage instead of any
  // time-varying spectral evolution, a real and audible difference from
  // additive's true per-partial decay worth hearing side by side. Sits
  // between Glass (very narrow/clean/bell-like) and the retuned Warm
  // (fuller): narrow-ish bandwidth for clarity/presence, a moderate
  // rolloff for a bit more bite/definition than Warm's own mellow one.
  static const PadSynthPresetParams kKeyboard{
    /* bandwidth_cents            */ 14.0f,
    /* bandwidth_scale_exponent   */ 0.6f,
    /* partial_count              */ 32,
    /* amplitude_rolloff_exponent */ 1.6f,
    /* formants                   */ {},
  };

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

  // A real bowed string is rich in harmonics with only a gentle rolloff
  // (closer to a sawtooth than a clean sine stack). The original design
  // tried to get "ensemble" beating purely from a wide, fast-growing
  // Gaussian bandwidth (no actual unison/detune voices) - first pass
  // (rolloff 0.9, bandwidthScale 1.0, 56 partials) read as mostly noise;
  // a tamer second pass (rolloff 1.3, bandwidthScale 0.7, 40 partials)
  // still read as "a buzzy drone swarm," with the reasonable suggestion
  // to get a single clean bowed instrument right before attempting
  // "ensemble" on top of it at all. Retuned as exactly that: bandwidth
  // brought down close to Warm's own (25 vs 18 cents, bandwidthScale
  // 0.5), so this preset's own distinguishing trait becomes almost
  // entirely its harmonic *profile* (a real, moderately-rolled-off
  // sawtooth-like richness - the amplitude_rolloff_exponent below) rather
  // than band-spreading; a small amount of extra bandwidth over Warm
  // supplies only a touch of natural live-bowing roughness, not a swarm.
  // Genuine multi-instrument ensemble beating (several truly independent,
  // detuned voices) isn't something a single PADsynth table can produce
  // convincingly - that would need real unison layering on top (e.g. a
  // parent <multiply>/NoteMultiplier, the way the additive piano's own
  // multi-string unison works), not attempted here.
  static const PadSynthPresetParams kBowedEnsemble{
    /* bandwidth_cents            */ 25.0f,
    /* bandwidth_scale_exponent   */ 0.5f,
    /* partial_count              */ 40,
    /* amplitude_rolloff_exponent */ 1.4f,
    /* formants                   */ {},
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

  // A Mellotron's "strings" tape was 3 real violins recorded per note - this
  // preset's own job is purely spectral character (what a massed bowed
  // string ensemble sounds like), not literal unison/detune voices; that
  // comes from <tapeDegradation preset="mellotron"> layered on top instead
  // (built separately - see docs/tape_degradation.md).
  // Close to Bowed Ensemble's own bandwidth (a bowed string ensemble is
  // exactly what a real Mellotron strings tape captured) but slightly
  // narrower/steeper, reading a little more "recorded tape" and a little
  // less "live" than Bowed Ensemble's own spread. Retuned twice alongside
  // Bowed Ensemble's own fixes even though not separately reported on -
  // this preset shared the identical shape each time, so it almost
  // certainly had the same problems; not yet verified by ear at all.
  static const PadSynthPresetParams kMellotron{
    /* bandwidth_cents            */ 20.0f,
    /* bandwidth_scale_exponent   */ 0.45f,
    /* partial_count              */ 32,
    /* amplitude_rolloff_exponent */ 1.7f,
    /* formants                   */ {},
  };

  // A pipe organ's flue pipes are about as close to PADsynth's own
  // idealized case as a real instrument gets - a steady, essentially
  // beat-free harmonic stack with no per-note decay of its own (the
  // "struck"/decaying quality every other preset's wrapping <envelope>
  // supplies instead comes from an organ's own on/off wind valve, not a
  // dying resonance - see InstrumentLibrary.cpp's own organ.pipe envelope:
  // near-instant attack, no decay stage, full sustain, quick release).
  // Narrower than even Glass (5 vs 8 cents) for that steadiness, with a
  // brighter/richer rolloff than Glass's own bell-like one (1.2 vs 1.5) -
  // a flue pipe is richer in upper harmonics than a struck bell/glass
  // tone, closer to a gentle sawtooth than a near-sine.
  static const PadSynthPresetParams kOrganPipe{
    /* bandwidth_cents            */ 5.0f,
    /* bandwidth_scale_exponent   */ 0.5f,
    /* partial_count              */ 32,
    /* amplitude_rolloff_exponent */ 1.2f,
    /* formants                   */ {},
  };

  // "church-organ" - unlike kOrganPipe above (a from-scratch, formula-only
  // approximation), this ports real data: the uploaded ZynAddSubFX factory
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
  static const PadSynthPresetParams kChurchOrgan{
    /* bandwidth_cents            */ 6.0f,
    /* bandwidth_scale_exponent   */ 0.5f,
    /* partial_count              */ 32,
    /* amplitude_rolloff_exponent */ 1.0f, // unused - harmonic_amplitudes below replaces it
    /* formants                   */ {},
    /* harmonic_amplitude_jitter  */ 0.0f,
    /* harmonic_amplitudes        */ {
      127.0f/127.0f, 111.0f/127.0f, 0.0f, 96.0f/127.0f,          //  1- 4
      0.0f, 0.0f, 0.0f, 92.0f/127.0f,                            //  5- 8
      0.0f, 0.0f, 0.0f, 74.0f/127.0f,                            //  9-12
      0.0f, 0.0f, 0.0f, 94.0f/127.0f,                            // 13-16
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 76.0f/127.0f,    // 17-24
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 74.0f/127.0f,    // 25-32
    },
  };

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
  static const PadSynthPresetParams kBells{
    /* bandwidth_cents            */ 8.0f,
    /* bandwidth_scale_exponent   */ 0.5f,
    /* partial_count              */ 8,
    /* amplitude_rolloff_exponent */ 1.0f, // unused - harmonic_amplitudes below replaces it
    /* formants                   */ {},
    /* harmonic_amplitude_jitter  */ 0.0f,
    /* harmonic_amplitudes        */ { 127.0f/127.0f, 75.0f/127.0f, 0.0f, 84.0f/127.0f },
  };

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

  if (name == "formant-vocal") return kFormantVocal;
  if (name == "choir-aah") return kFormantVocal;
  if (name == "choir-ooh") return kChoirOoh;
  if (name == "bowed-ensemble") return kBowedEnsemble;
  if (name == "glass") return kGlass;
  if (name == "mellotron") return kMellotron;
  if (name == "keyboard") return kKeyboard;
  if (name == "organ-pipe") return kOrganPipe;
  if (name == "church-organ") return kChurchOrgan;
  if (name == "bells") return kBells;
  if (name == "synth-brass") return kSynthBrass;
  return kWarm;
}

#endif
