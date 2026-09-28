#include "InstrumentLibrary.h"

#include "InstrumentProvider.h"
#include "PadSynth.h"
#include "Additive.h"
#include "NoteMultiplier.h"
#include "../effects/EnvelopeFilter.h"
#include "../effects/TapeDegradation.h"
#include "../effects/BiquadFilter.h"
#include "../effects/Phaser.h"
#include "../state/MemoryParameterSource.h"
#include "../ambisonic/ChannelConfiguration.h"

#include <memory>
#include <optional>
#include <string>

using namespace std;

namespace {

unique_ptr<EnvelopeFilter> makeEnvelope(float attack, float hold, float decay, float sustain, float release) {
  auto env = make_unique<EnvelopeFilter>();
  MemoryParameterSource params;
  params.set("attack", attack);
  params.set("hold", hold);
  params.set("decay", decay);
  params.set("sustain", sustain);
  params.set("release", release);
  env->loadParameters(params);
  return env;
}

// bandwidth/bandwidthScale/tuningMatched left unset (nullopt) mean "keep
// the named preset's own default" - only set them to actually differentiate
// a pad from another one sharing the same base preset (see the GM pad
// registrations below: several pairs share a preset by design, since only
// 5 padsynth presets exist for 8 GM pads, but envelope timing alone isn't
// audible enough to tell a static pad tone apart - confirmed by an actual
// listen, "pad.choir and pad.halo sound exactly the same" - so these
// overrides are what actually keeps each pad distinct).
unique_ptr<PadSynth> makePadSynth(const string & preset, optional<float> bandwidth = nullopt,
                                   optional<float> bandwidth_scale = nullopt, optional<bool> tuning_matched = nullopt) {
  auto pad = make_unique<PadSynth>();
  MemoryParameterSource params;
  params.set("preset", preset);
  if (bandwidth) params.set("bandwidth", *bandwidth);
  if (bandwidth_scale) params.set("bandwidthScale", *bandwidth_scale);
  if (tuning_matched) params.set("tuningMatched", *tuning_matched);
  pad->loadParameters(params);
  return pad;
}

// <envelope>+<padsynth> - the shape every GM synth-pad override and the
// mellotron are built from (docs/effects.md's own canonical nesting
// pattern), assembled directly in C++ via MemoryParameterSource rather
// than parsed from an XML string, the same round-trip-without-XML
// technique SongState::initialize() already uses for bus-slot effects.
unique_ptr<Track> makeEnvelopePad(const string & padsynth_preset, float attack, float hold, float decay, float sustain, float release,
                                   optional<float> bandwidth = nullopt, optional<float> bandwidth_scale = nullopt,
                                   optional<bool> tuning_matched = nullopt) {
  auto env = makeEnvelope(attack, hold, decay, sustain, release);
  env->addChild(makePadSynth(padsynth_preset, bandwidth, bandwidth_scale, tuning_matched));
  return env;
}

// <multiply>+envelope+padsynth - the "real unison layering" PadSynthPresets.h's
// own bowed-ensemble comment named as the one thing a single static
// PADsynth table can never produce on its own: genuine ensemble beating
// needs several truly independent voices, each landing on a slightly
// different pitch, not just a wider Gaussian band (which only spreads one
// voice's own partials, with no time-varying interference between
// separately-drifting singers/instruments at all). <multiply>
// (NoteMultiplier) already exists for exactly this - a generic
// unison/detune/spread wrapper usable around any child instrument, not
// PadSynth-specific - so this reuses it rather than adding anything new to
// PadSynthWavetable's own math. Each voice reads the identical shared
// wavetable (unisons this small keep every voice within the same cached
// octave region - no extra table builds), just resampled at its own
// slightly-detuned pitch, exactly the way a real ensemble's own singers/
// players never quite land on the identical pitch or timing.
unique_ptr<Track> makeUnisonPad(const string & padsynth_preset, int unisons, float detune_cents, float spread,
                                 float attack, float hold, float decay, float sustain, float release) {
  auto multiplier = make_unique<NoteMultiplier>();
  MemoryParameterSource params;
  params.set("unisons", unisons);
  params.set("detune", detune_cents);
  params.set("spread", spread);
  multiplier->loadParameters(params);
  multiplier->addChild(makeEnvelopePad(padsynth_preset, attack, hold, decay, sustain, release));
  return multiplier;
}

// Registers `path` only when nothing (no SoundFont, no earlier library
// registration) has already claimed that exact taxonomy leaf - the
// "fallback when the SoundFont has no piano" half of the additive piano's
// job. Deliberately an exact-key check (getTaxonomyPaths(), not
// resolvePath()'s walk-up): resolvePath("piano.acoustic.grand") could
// still find something via a shorter prefix or a kGmPathDefaults redirect
// even with no exact SF2 registration at this leaf, and that's a real,
// intentional fallback of its own - this function only cares whether this
// exact leaf is unclaimed.
void registerFallbackPath(InstrumentProvider & provider, const string & path, shared_ptr<Track> instrument) {
  if (provider.getTaxonomyPaths().count(path)) return;
  provider.registerPath(path, move(instrument));
}

}

void registerLibraryInstruments(InstrumentProvider & provider) {
  // GM synth pads (programs 89-96) - each <envelope>+<padsynth>, overriding
  // whatever loadSoundFont() registered at the same pad.* path through the
  // ordinary registerPath() last-write-wins rule, not by special-casing
  // GenericInstrument's resolution. ADSR/preset choices below aim for each
  // pad's own GM character; see this function's own end for the one pad
  // (Sweep) an oscillator-based instrument can't fully deliver.
  //
  // Only 5 base padsynth spectral shapes exist for 8 GM pads (choir-aah/
  // choir-ooh share formant-vocal's underlying shape family - see
  // PadSynthPresets.h), so three pairs below
  // deliberately share a base preset (newAge/metallic both "glass",
  // choir/halo both "formant-vocal", warm/poly both "warm") - an actual
  // listen found envelope timing alone isn't audible enough to tell a
  // static pad tone apart ("pad.choir and pad.halo sound exactly the
  // same," same for metallic/newAge), so each pair now also gets an
  // explicit bandwidth/bandwidthScale/tuningMatched override on top of its
  // shared preset, specifically chosen to give the two members of each
  // pair a genuinely different texture, not just a different envelope.
  provider.registerPath("pad.newAge", makeEnvelopePad("glass", 0.8f, 0.0f, 0.3f, 0.9f, 1.2f));
  provider.registerPath("pad.warm", makeEnvelopePad("warm", 0.6f, 0.0f, 0.4f, 0.85f, 1.0f));
  // Poly: brighter/more chorused than Warm - a classic analog polysynth
  // pad reads as more "moving"/present than a plain warm pad, via a wider
  // bandwidth than Warm's own default alone (not just a faster attack).
  provider.registerPath("pad.poly", makeEnvelopePad("warm", 0.2f, 0.0f, 0.3f, 0.8f, 0.6f, /*bandwidth*/ 32.0f, /*bandwidthScale*/ 0.7f));
  // Choir: two registrations, not three. "pad.choir" is GM program 91's
  // own literal taxonomy path (GmInstrumentTable.h) - it has to exist
  // under exactly that name for the ordinary GM override behavior every
  // other pad.* entry here relies on to work at all (resolvePath() only
  // ever walks from a request *up* to shorter prefixes, never down into a
  // more specific child, so "pad.choir.aah" alone would leave a plain
  // "pad.choir" request falling through to the unrelated pad.warm
  // default - see docs/padsynth.md). Since GM's own Choir Aahs patch is
  // specifically the open "ah" vowel, "pad.choir" itself IS the aah
  // variant - no separate "pad.choir.aah" alias needed on top of it.
  // "pad.choir.ooh" is the one addition beyond GM (which has no separate
  // "ooh" choir program) - a second, genuinely different vowel character
  // (a real ZynAddSubFX factory choir patch's own example parameters were
  // checked for anything directly borrowable here; its bandwidth/
  // bandwidthScale values turned out to cluster tightly across every one
  // of its patches regardless of instrument character, so there was
  // nothing preset-specific to port - see docs/padsynth.md).
  // 3 unison voices, 16 cents detune spread, moderate stereo spread - see
  // makeUnisonPad()'s own comment: this is what actually gives the choir
  // its ensemble-of-singers motion, not any padsynth-side parameter alone.
  provider.registerPath("pad.choir", makeUnisonPad("choir-aah", 3, 16.0f, 0.5f, 0.5f, 0.0f, 0.3f, 0.9f, 0.8f));
  provider.registerPath("pad.choir.ooh", makeUnisonPad("choir-ooh", 3, 16.0f, 0.5f, 0.6f, 0.0f, 0.35f, 0.9f, 0.9f));
  provider.registerPath("pad.bowed", makeEnvelopePad("bowed-ensemble", 0.4f, 0.0f, 0.3f, 0.9f, 0.7f));
  // Metallic: Glass's own clean/bell-like base, but tuningMatched=false -
  // inharmonic (non-scale-step) overtones, which is what actually reads as
  // "metallic"/bell-like dissonance (a real bell's overtones are famously
  // non-integer) rather than Glass's own clean, consonant partials -
  // plus a wider bandwidth for more clangorous beating.
  provider.registerPath("pad.metallic", makeEnvelopePad("glass", 0.3f, 0.0f, 0.5f, 0.7f, 1.0f, /*bandwidth*/ 20.0f, /*bandwidthScale*/ 0.7f, /*tuningMatched*/ false));
  // Halo: Choir's own formant-vocal base, wider/faster-growing bandwidth
  // for a more diffuse texture than Choir's own focused, speech-like one -
  // now also wrapped in <phaser>. A real ZynAddSubFX factory choir patch
  // ("Long SpaceChoir2", checked among the example data referenced
  // elsewhere in this file) is specifically a *phased* choir pad - slow,
  // sweeping notches are exactly the "shimmering, ethereal" motion GM's
  // own Halo name implies, and a static PADsynth table alone can't
  // produce it any more than it can produce real ensemble beating (the
  // same "needs real motion layered on top" reasoning as pad.choir's own
  // <multiply> unison - a different kind of motion, same underlying gap).
  // A slow rate (0.15Hz - one full sweep every ~6.7s) keeps it a slow
  // shimmer, not an obvious/fast "phaser pedal" swoosh.
  {
    auto phaser = make_unique<Phaser>();
    MemoryParameterSource phaser_params;
    phaser_params.set("stages", 6);
    phaser_params.set("rate", 0.15f);
    phaser_params.set("minFreq", 300.0f);
    phaser_params.set("maxFreq", 2500.0f);
    phaser_params.set("feedback", 0.3f);
    phaser_params.set("mix", 0.5f);
    phaser->loadParameters(phaser_params);
    phaser->addChild(makeEnvelopePad("formant-vocal", 1.0f, 0.0f, 0.4f, 0.9f, 1.5f, /*bandwidth*/ 40.0f, /*bandwidthScale*/ 0.8f));
    provider.registerPath("pad.halo", move(phaser));
  }

  // Sweep (pad.sweep) is a slow filter sweep over the note's own life -
  // motion no static oscillator (PADsynth included) can produce; there is
  // no LFO-modulated per-voice filter in this codebase to drive one
  // dynamically (see this function's own doc comment / the task's final
  // report for this open item). Approximated instead with a static
  // <biquadFilter> darkening a warm pad - a real, audible difference from
  // the other pads, just not the sweeping motion GM's own Sweep implies.
  {
    auto filter = make_unique<BiquadFilter>();
    MemoryParameterSource filter_params;
    filter_params.set("type", string("lowpass"));
    // fc is a raw Hz value (BiquadFilter::createVoiceState() normalizes it
    // by the output sample rate internally, see BiquadFilter.cpp) - NOT a
    // pre-normalized 0.0-0.5 fraction, despite docs/effects.md's own (now
    // corrected) claim to the contrary. The original fc="0.15" was
    // effectively an inaudible near-zero-Hz cutoff (0.15/48000), which is
    // why this preset was reported completely silent - confirmed against
    // songs/subtractive_test.xml's own real usage (fc="2000").
    filter_params.set("fc", 2000.0f);
    filter_params.set("Q", 0.7071f); // Butterworth Q - BiquadFilter has no default, 0 would divide by zero
    filter->loadParameters(filter_params);
    filter->addChild(makeEnvelopePad("warm", 0.9f, 0.0f, 0.4f, 0.85f, 1.2f));
    provider.registerPath("pad.sweep", move(filter));
  }

  // A few PadSynth overrides outside the pad.* family - checked against
  // every other GM program family (leads especially, per an explicit
  // request) for where PADsynth's own character (a Gaussian-band
  // resynthesis, inherently a little soft/diffuse even at its narrowest,
  // never a crisp single-cycle waveform) is actually a good fit, not a
  // regression against a plain analog-style Oscillator. Most of GM's 8
  // leads are a poor fit precisely because they want that crisp precision
  // (Lead 1 Square/Lead 2 Sawtooth: a plain Oscillator already is the
  // correct, better tool; Lead 4 Chiff needs a noise-burst attack
  // transient, not a spectral-shape choice; Lead 5 Charang wants
  // Distortion on a sharp Oscillator; Lead 7 Fifths is `<multiply
  // fifths="1">` layered on a plain Oscillator, not a spectral question at
  // all; Lead 8 Bass+Lead is a two-Oscillator layering problem) - none of
  // that changes by swapping in PadSynth underneath. Lead 6 (Voice) is the
  // one genuine exception: a real "synth voice" lead patch already wants
  // exactly the vowel-formant, slightly-soft character `choir-aah` was
  // built for, just played as a focused melodic lead rather than a
  // sustained chorused pad - fast attack/release, no unison layered on
  // (a lead stays one focused voice; that's what tells it apart from a
  // choir pad using the identical spectral shape).
  provider.registerPath("lead.voice", makeEnvelopePad("choir-aah", 0.03f, 0.0f, 0.15f, 0.9f, 0.2f));

  // Church Organ (organ.pipe) - unlike lead.voice above (GM's own "Lead"
  // family is a synth-lead category by definition, so overriding it
  // unconditionally the same way every pad.* entry does is correct),
  // Church Organ is a real acoustic instrument, and GM's organ family has
  // no separate synth-organ program the way strings did (string.synth) to
  // redirect to instead. A real SoundFont's own recorded/sampled organ
  // will always be more convincing than this resynthesis, and shouldn't
  // lose to it - registered as a fallback (registerFallbackPath(), the
  // same "only fill the leaf in if a SoundFont didn't already claim it"
  // role piano.acoustic.grand/the additive piano use), not an
  // unconditional override. See PadSynthPresets.h's own "organ-pipe"
  // comment for why a pipe organ is nonetheless close to PADsynth's ideal
  // case *when nothing better is available*: a single organ pipe's own
  // tone is a steady, near-beat-free standing wave, not several
  // independent players drifting relative to each other the way a bowed
  // ensemble needs (the mistake string.bowed.ensemble.slow's own
  // registration made, now string.synth.slow instead) - so the "PADsynth
  // can't fake real human/ensemble variance" objection doesn't apply here
  // the same way, even though "a real recording still wins when one
  // exists" still does. The envelope is what actually carries the organ
  // character here, at least as much as the preset does - no decay stage
  // and full sustain (an organ holds at exactly one level for as long as
  // the key/wind valve stays open, never decaying on its own the way a
  // struck/plucked/bowed instrument does), near-instant attack and a
  // quick release (no swell, no ring-on).
  registerFallbackPath(provider, "organ.pipe", makeEnvelopePad("organ-pipe", 0.015f, 0.0f, 0.05f, 1.0f, 0.08f));

  // Synth Strings 2 (string.synth.slow) - NOT String Ensemble 2
  // (string.bowed.ensemble.slow, under the string.bowed.* branch with
  // violin/viola/cello/contrabass): that path means a real, acoustic
  // bowed-string section, which a PADsynth resynthesis has no business
  // standing in for - a SoundFont's own recorded/sampled strings are the
  // right (and only convincing) source there, left untouched. GM's own
  // Synth Strings 1/2 (programs 50-51) are the deliberately synthetic
  // pair, exactly what bowed-ensemble's own harmonic profile plus real
  // <multiply> unison for genuine multi-player beating (not just a wide
  // Gaussian band - see docs/padsynth.md's own "Known limitations") is
  // actually suited to. "slow" (2, not 1) since that's the sustained-pad
  // half of the pair - the same reasoning "String Ensemble 2"'s own
  // "slow" naming already established for the acoustic side.
  provider.registerPath("string.synth.slow", makeUnisonPad("bowed-ensemble", 3, 14.0f, 0.5f, 0.5f, 0.0f, 0.4f, 0.85f, 0.9f));

  // Additive piano - <envelope>+<additive preset="struck-string">, with a
  // few explicit overrides on top of the base preset rather than retuning
  // "struck-string" itself (which stays the generic, guitar-reads-fine
  // plucked-string starting point usable standalone). Heard by ear as
  // reading more like a single plucked nylon string than a piano with the
  // bare preset - three targeted differences a piano actually has against
  // a guitar's single string per note:
  //  - unisonVoices=3: a real piano doubles or triples each note's string
  //    in the mid/treble register (a guitar has exactly one string per
  //    note) - the base preset's 2 voices likely read as too subtly
  //    chorused to sound like multiple strings at all.
  //  - attackNoiseLevel lower (0.04 vs the base preset's 0.08): a felt
  //    hammer strike is a softer, duller transient than a plucked/
  //    fingerpicked nylon string's own sharper attack noise.
  //  - tilt less negative (-6 vs -9 dB/octave): a hammer-struck string
  //    reads brighter/fuller than a plucked one at the same register.
  // Sustain 0 (a piano's own sound is entirely percussive/decaying, never
  // a held plateau) with a long decay stage (8s, well past where the
  // additive engine's own per-partial decay has already gone inaudible -
  // see SinusoidBank's -90dB culling) so the envelope itself never audibly
  // truncates the tail, and a short release (0.3s) so lifting the key ends
  // the note promptly rather than ringing on indefinitely.
  auto additive_piano_envelope = []() {
    auto env = makeEnvelope(0.005f, 0.0f, 8.0f, 0.0f, 0.3f);
    auto additive = make_unique<Additive>();
    MemoryParameterSource additive_params;
    additive_params.set("preset", string("struck-string"));
    additive_params.set("unisonVoices", 3);
    additive_params.set("attackNoiseLevel", 0.04f);
    additive_params.set("tilt", -6.0f);
    additive->loadParameters(additive_params);
    env->addChild(move(additive));
    return env;
  };

  // Always registered at its own dedicated leaf so it can be forced for
  // comparison regardless of whether a SoundFont piano exists.
  provider.registerPath("piano.additive", additive_piano_envelope());
  // Only takes over piano.acoustic.grand itself when nothing already
  // claimed that exact leaf - the "fallback when the SoundFont has no
  // piano" role; a real SF2 grand piano always wins when one is loaded.
  registerFallbackPath(provider, "piano.acoustic.grand", additive_piano_envelope());

  // Mellotron - tapeDegradation(preset="mellotron") wrapping
  // envelope+padsynth(preset="mellotron"), the exact tape+envelope+
  // oscillator nesting docs/tape_degradation.md already documents for a
  // voice-attached tape machine. Not a General MIDI instrument - GM has no
  // mellotron program - so it's registered under the existing `keyboard`
  // taxonomy root instead, following the same root.technology.name shape
  // GM's own electric-keyboard entries use (piano.electric.tine,
  // keyboard.electric.clavinet): keyboard.tape.mellotron. OutlineView's
  // Library section lists every taxonomy path generically (it doesn't
  // distinguish GM-backed from hand-registered entries), so this shows up
  // there automatically, sorted alongside the other keyboard.* entries.
  {
    auto tape = make_unique<TapeDegradation>();
    MemoryParameterSource tape_params;
    tape_params.set("preset", string("mellotron"));
    tape->loadParameters(tape_params);
    tape->addChild(makeEnvelopePad("mellotron", 0.05f, 0.0f, 0.3f, 0.95f, 0.4f));
    provider.registerPath("keyboard.tape.mellotron", move(tape));
  }
}

namespace {

// A4 (440Hz) in each tuning's own note-value numbering - Tuning.h/Note.h's
// own per-tuning center, duplicated here in miniature rather than shared,
// since this is the only place outside Tuning.h itself that ever needs a
// plain "a reasonable middle-register note" rather than a specific
// authored pitch.
int centerNoteFor(Tuning tuning) {
  switch (tuning) {
  case Tuning::TET12: return 69;
  case Tuning::TET19: return 109;
  case Tuning::TET31: return 178;
  case Tuning::TET53: return 304;
  case Tuning::PERCUSSION: return 69;
  }
  return 178;
}

}

void prewarmInstrumentTree(const Track & track, const ChannelConfiguration & config, Tuning tuning, int note_value) {
  if (auto * padsynth = dynamic_cast<const PadSynth *>(&track)) {
    padsynth->prewarm(config, tuning, note_value);
  }
  for (auto & child : track.getChildren()) {
    prewarmInstrumentTree(*child, config, tuning, note_value);
  }
}

void prewarmLibraryInstruments(InstrumentProvider & provider, const ChannelConfiguration & config, Tuning tuning) {
  int note_value = centerNoteFor(tuning);
  for (auto & entry : provider.getTaxonomyPaths()) {
    if (entry.second) prewarmInstrumentTree(*entry.second, config, tuning, note_value);
  }
}
