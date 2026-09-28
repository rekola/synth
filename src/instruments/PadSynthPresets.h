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
  static const PadSynthPresetParams kKeyboard{
    /* bandwidth_cents            */ 12.0f,
    /* bandwidth_scale_exponent   */ 0.6f,
    /* partial_count              */ 6,
    /* amplitude_rolloff_exponent */ 1.0f, // unused - harmonic_amplitudes below replaces it
    /* formants                   */ {},
    /* harmonic_amplitude_jitter  */ 0.0f,
    /* harmonic_amplitudes        */ { 127.0f/127.0f, 127.0f/127.0f, 0.0f, 0.0f, 99.0f/127.0f, 104.0f/127.0f },
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
  // Caveat worth flagging: the real patch's own `adaptive_harmonics` is 2
  // (nonzero), meaning ZynAddSubFX's own internal harmonic generator
  // contributes to the real spectrum beyond just these 3 explicit
  // entries - that generator is GPL application logic, not exposed
  // parameter data, so it stays off-limits (see docs/padsynth.md). This
  // preset treats the 3 explicit harmonics as the *complete* spectrum,
  // which is the most this codebase can faithfully claim from the data
  // alone - a reasonable approximation, not a byte-for-byte match to
  // what the real patch actually sounds like.
  static const PadSynthPresetParams kBells{
    /* bandwidth_cents            */ 8.0f,
    /* bandwidth_scale_exponent   */ 0.5f,
    /* partial_count              */ 8,
    /* amplitude_rolloff_exponent */ 1.0f, // unused - harmonic_amplitudes below replaces it
    /* formants                   */ {},
    /* harmonic_amplitude_jitter  */ 0.0f,
    /* harmonic_amplitudes        */ { 127.0f/127.0f, 75.0f/127.0f, 0.0f, 84.0f/127.0f },
  };

  // "strings" - the real "Strings" patch (noefx_bell_strings.xmz, PART
  // id="1") - unlike Bells above, its own adaptive_harmonics is 0, so its
  // 2 explicit harmonics (1/2, mag 127/117) genuinely are the complete
  // spectrum, not a partial view of something a GPL-internal generator
  // also shapes. A very simple, near-octave-doubled tone on its own -
  // the real "ensemble" character comes from unison layering on top (see
  // InstrumentLibrary.cpp's own string.synth.slow registration), same
  // reasoning as this codebase's own earlier bowed-ensemble+<multiply>
  // choice.
  static const PadSynthPresetParams kStrings{
    /* bandwidth_cents            */ 10.0f,
    /* bandwidth_scale_exponent   */ 0.5f,
    /* partial_count              */ 2,
    /* amplitude_rolloff_exponent */ 1.0f, // unused - harmonic_amplitudes below replaces it
    /* formants                   */ {},
    /* harmonic_amplitude_jitter  */ 0.0f,
    /* harmonic_amplitudes        */ { 127.0f/127.0f, 117.0f/127.0f },
  };

  // "saw-piano" - the real "Saw Piano 1" patch (noefx_saw_piano.xmz,
  // adaptive_harmonics=0, so fully portable): 4 explicit harmonics
  // (1/2/4/16, mag 127/123/127/100). Notably its own amplitude envelope
  // sustains at S_val=127 (full level, no decay-to-silence) rather than
  // Synth Piano 3's own S_val=0 - a sustained "piano pad" character, not
  // a percussive one; kept that way here rather than assumed-percussive,
  // since the data says otherwise (see InstrumentLibrary.cpp's/the demo
  // song's own envelope for this preset).
  static const PadSynthPresetParams kSawPiano{
    /* bandwidth_cents            */ 8.0f,
    /* bandwidth_scale_exponent   */ 0.5f,
    /* partial_count              */ 16,
    /* amplitude_rolloff_exponent */ 1.0f, // unused - harmonic_amplitudes below replaces it
    /* formants                   */ {},
    /* harmonic_amplitude_jitter  */ 0.0f,
    /* harmonic_amplitudes        */ {
      127.0f/127.0f, 123.0f/127.0f, 0.0f, 127.0f/127.0f,   //  1- 4
      0.0f, 0.0f, 0.0f, 0.0f,                              //  5- 8
      0.0f, 0.0f, 0.0f, 0.0f,                              //  9-12
      0.0f, 0.0f, 0.0f, 100.0f/127.0f,                     // 13-16
    },
  };

  // "soft-pad" - the real "Soft Pad" patch (noefx_soft_pad.xmz),
  // adaptive_harmonics=0 so fully portable, and genuinely just harmonic 1
  // - a pure, single-partial tone is exactly what "soft" means spectrally
  // (no upper harmonics to roughen it at all), not an approximation this
  // time. A_dt=31/D_dt=40/S_val=127/R_dt=87 (see InstrumentLibrary.cpp's/
  // the demo song's own envelope for this preset) - moderate attack,
  // settling at full sustain, longish release.
  static const PadSynthPresetParams kSoftPad{
    /* bandwidth_cents            */ 10.0f,
    /* bandwidth_scale_exponent   */ 0.5f,
    /* partial_count              */ 1,
    /* amplitude_rolloff_exponent */ 1.0f, // unused - harmonic_amplitudes below replaces it
    /* formants                   */ {},
    /* harmonic_amplitude_jitter  */ 0.0f,
    /* harmonic_amplitudes        */ { 1.0f },
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
  if (name == "glass") return kGlass;
  if (name == "mellotron") return kMellotron;
  if (name == "keyboard") return kKeyboard;
  if (name == "church-organ") return kChurchOrgan;
  if (name == "bells") return kBells;
  if (name == "strings") return kStrings;
  if (name == "saw-piano") return kSawPiano;
  if (name == "soft-pad") return kSoftPad;
  if (name == "synth-brass") return kSynthBrass;
  return kWarm;
}

#endif
