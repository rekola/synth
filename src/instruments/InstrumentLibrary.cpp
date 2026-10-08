#include "InstrumentLibrary.h"

#include "InstrumentProvider.h"
#include "PadSynth.h"
#include "Additive.h"
#include "FM.h"
#include "../effects/EnvelopeFilter.h"
#include "../model/Group.h"
#include "../effects/TapeDegradation.h"
#include "../effects/ResonantFilter.h"
#include "../effects/Phaser.h"
#include "../state/MemoryParameterSource.h"
#include "../ambisonic/ChannelConfiguration.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

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

// tuning_matched left unset (nullopt) means "keep the named preset's own
// default" - only set it to actually differentiate a pad from another one
// sharing the same base preset.
unique_ptr<PadSynth> makePadSynth(const string & preset, optional<bool> tuning_matched = nullopt, float detune_cents = 0.0f) {
  auto pad = make_unique<PadSynth>();
  MemoryParameterSource params;
  params.set("preset", preset);
  if (tuning_matched) params.set("tuningMatched", *tuning_matched);
  if (detune_cents != 0.0f) params.set("detune", detune_cents);
  pad->loadParameters(params);
  return pad;
}

// <envelope>+<padsynth> - the shape every GM synth-pad override is built
// from (docs/effects.md's own canonical nesting pattern), assembled
// directly in C++ via MemoryParameterSource rather than parsed from an
// XML string, the same round-trip-without-XML technique SongState::
// initialize() already uses for bus-slot effects.
unique_ptr<Track> makeEnvelopePad(const string & padsynth_preset, float attack, float hold, float decay, float sustain, float release,
                                   optional<bool> tuning_matched = nullopt) {
  auto env = makeEnvelope(attack, hold, decay, sustain, release);
  env->addChild(makePadSynth(padsynth_preset, tuning_matched));
  return env;
}

// <envelope>+<group> of `copies` <padsynth> voices spread evenly and
// centred across `detune_cents` - genuine ensemble beating needs several
// truly independent voices, each landing on a slightly different pitch, not
// just a single voice's own partials (which have no time-varying
// interference between separately-drifting singers/players at all). Every
// copy reads the same shared wavetable, resampled at its own pitch.
unique_ptr<Track> makeEnsemblePad(const string & padsynth_preset, int copies, float detune_cents,
                                   float attack, float hold, float decay, float sustain, float release) {
  auto env = makeEnvelope(attack, hold, decay, sustain, release);
  auto group = make_unique<Group>();
  for (int k = 0; k < copies; k++) {
    float cents = copies > 1 ? -detune_cents / 2.0f + detune_cents * static_cast<float>(k) / static_cast<float>(copies - 1) : 0.0f;
    group->addChild(makePadSynth(padsynth_preset, nullopt, cents));
  }
  env->addChild(std::move(group));
  return env;
}

// Registers `instrument` at `path` under a display name; library instruments
// have none of their own, so the UI would otherwise show the path's last
// segment.
void registerNamed(InstrumentProvider & provider, const string & path, const string & name, unique_ptr<Track> instrument) {
  instrument->setName(name);
  provider.registerPath(path, move(instrument));
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
void registerFallbackPath(InstrumentProvider & provider, const string & path, const string & name, shared_ptr<Track> instrument) {
  if (provider.getTaxonomyPaths().count(path)) return;
  instrument->setName(name);
  provider.registerPath(path, move(instrument));
}

// One layer of an instrument after a DX7 voice: an <fm> pair in its own
// <envelope>. Every index tracks the pitch, so low notes are as bright and
// as loud as high ones, and the decays grow by about 1.4 an octave down, so
// bass notes ring longer. `decay` is the time to -80 dB at middle C. A DX7
// carrier off the note's pitch becomes a detune in cents, with the
// modulator's ratio taken relative to that carrier.
struct FMLayer {
  float ratio, index, index_decay, detune_cents, level, decay, feedback = 0.0f;
  float attack = 0.002f, sustain = 0.0f, release = 0.25f;
};

unique_ptr<Track> makeFMLayers(const vector<FMLayer> & layers) {
  auto group = make_unique<Group>();
  for (const auto & layer : layers) {
    auto fm = make_unique<FM>();
    MemoryParameterSource params;
    params.set("ratio", layer.ratio);
    params.set("index", layer.index);
    params.set("indexDecay", layer.index_decay);
    params.set("indexTracking", 1.0f);
    params.set("indexDecayTracking", 0.5f);
    params.set("feedback", layer.feedback);
    params.set("detune", layer.detune_cents);
    params.set("level", layer.level);
    fm->loadParameters(params);
    auto envelope = make_unique<EnvelopeFilter>();
    MemoryParameterSource envelope_params;
    envelope_params.set("attack", layer.attack);
    envelope_params.set("decay", layer.decay);
    envelope_params.set("sustain", layer.sustain);
    envelope_params.set("release", layer.release);
    envelope_params.set("keynumToDecay", 50.0f);
    envelope->loadParameters(envelope_params);
    envelope->addChild(move(fm));
    group->addChild(move(envelope));
  }
  return group;
}
}

void registerLibraryInstruments(InstrumentProvider & provider) {
  // GM synth pads (programs 89-96) - each <envelope>+<padsynth>, overriding
  // whatever loadSoundFont() registered at the same pad.* path through the
  // ordinary registerPath() last-write-wins rule, not by special-casing
  // GenericInstrument's resolution. ADSR/preset choices below aim for each
  // pad's own GM character.
  //
  // GM's own description for New Age is "a soft, airy new-age pad" -
  // "soft-pad" (a single pure partial, no upper harmonics) is a direct
  // semantic match. Warm uses "strings" (a simple, mellow base tone).
  registerNamed(provider, "pad.new-age", "New Age Pad", makeEnvelopePad("soft-pad", 0.8f, 0.0f, 0.3f, 0.9f, 1.2f));
  registerNamed(provider, "pad.warm", "Warm Pad", makeEnvelopePad("strings", 0.6f, 0.0f, 0.4f, 0.85f, 1.0f));
  // Poly: "dual-strings" is the brighter/wider of the two strings-family
  // presets, closest to a vintage poly synth; a 3-copy ensemble gives the
  // chorus motion its GM description calls for.
  registerNamed(provider, "pad.poly", "Polysynth Pad", makeEnsemblePad("dual-strings", 3, 10.0f, 0.2f, 0.0f, 0.3f, 0.8f, 0.6f));
  // Choir: "pad.choir" is GM program 91's own literal taxonomy path
  // (GmInstrumentTable.h) - it has to exist under exactly that name for
  // the ordinary GM override behavior every other pad.* entry here relies
  // on to work at all (resolvePath() only ever walks from a request *up*
  // to shorter prefixes, never down into a more specific child, so
  // "pad.choir.aah" alone would leave a plain "pad.choir" request falling
  // through to the unrelated pad.warm default - see docs/padsynth.md).
  // Since GM's own Choir Aahs patch is specifically the open "ah" vowel,
  // "pad.choir" itself IS the aah variant - no separate "pad.choir.aah"
  // alias needed on top of it. A second, "ooh" vowel variant was tried and
  // pulled again (didn't actually read as a distinct vowel) - not
  // registered until a real one exists. A 3-copy ensemble gives the choir
  // its ensemble-of-singers motion.
  registerNamed(provider, "pad.choir", "Choir Pad", makeEnsemblePad("choir-pad4", 3, 16.0f, 0.5f, 0.0f, 0.3f, 0.9f, 0.8f));
  // Bowed: the "strings" preset's own simple base tone, as a 3-copy ensemble.
  registerNamed(provider, "pad.bowed", "Bowed Pad", makeEnsemblePad("strings", 3, 14.0f, 0.4f, 0.0f, 0.3f, 0.9f, 0.7f));
  // Metallic, after the DX7's factory T.BL-EXPA: two 3.5:1 tubular-bell
  // strikes over a 1:1 pad with feedback that swells in and holds (see the
  // FM electric pianos below for the layering).
  registerNamed(provider, "pad.metallic", "Metallic Pad", makeFMLayers({
                                                              {3.5f, 1.45f, 0.7f, 1.0f, 0.25f, 20.0f},
                                                              {3.5f, 0.8f, 4.0f, -2.0f, 0.25f, 4.0f},
                                                              {1.002f, 1.5f, 0.0f, -1.0f, 0.3f, 0.0f, 0.4f, 1.5f, 1.0f, 2.5f},
                                                          }));
  // Halo: "long-spacechoir2" is specifically a *phased* choir pad, so its
  // own envelope-remap character plus the wrapping <phaser> below together
  // give it its own shimmering motion. A slow rate (0.15Hz - one full
  // sweep every ~6.7s) keeps it a slow shimmer, not an obvious/fast
  // "phaser pedal" swoosh.
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
    phaser->addChild(makeEnvelopePad("long-spacechoir2", 1.0f, 0.0f, 0.4f, 0.9f, 1.5f));
    registerNamed(provider, "pad.halo", "Halo Pad", move(phaser));
  }

  // Sweep (pad.sweep) is a slow filter sweep: a <resonantFilter>'s own
  // envelope opens the cutoff over the attack and holds it open, over a plain
  // pad.
  {
    auto filter = make_unique<ResonantFilter>();
    MemoryParameterSource filter_params;
    filter_params.set("cutmin", 300.0f);
    filter_params.set("cutmax", 9000.0f);
    filter_params.set("res", 0.5f);
    filter_params.set("attack", 1.5f);
    filter_params.set("decay", 0.0f);
    filter_params.set("sustain", 1.0f);
    filter_params.set("release", 1.2f);
    filter->loadParameters(filter_params);
    filter->addChild(makeEnvelopePad("strings", 0.3f, 0.0f, 0.4f, 0.85f, 1.2f));
    registerNamed(provider, "pad.sweep", "Sweep Pad", move(filter));
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
  // Distortion on a sharp Oscillator; Lead 7 Fifths is two
  // plain Oscillators a fifth apart, not a spectral question at all; Lead 8 Bass+Lead is a two-Oscillator layering problem) - none of
  // that changes by swapping in PadSynth underneath. Lead 6 (Voice) is the
  // one genuine exception: a real "synth voice" lead patch wants exactly
  // the vowel-formant, slightly-soft character `choir-pad4` was built for
  // (see pad.choir's own comment), just played as a focused melodic lead
  // rather than a sustained chorused pad - fast attack/release, no unison
  // layered on (a lead stays one focused voice; that's what tells it apart
  // from a choir pad using the identical spectral shape).
  registerNamed(provider, "lead.voice", "Voice Lead", makeEnvelopePad("choir-pad4", 0.03f, 0.0f, 0.15f, 0.9f, 0.2f));

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
  registerFallbackPath(provider, "organ.pipe", "Pipe Organ", makeEnvelopePad("church-organ", 0.015f, 0.0f, 0.05f, 1.0f, 0.08f));

  // Synth Strings 2 (string.synth.slow) - NOT String Ensemble 2
  // (string.bowed.ensemble.slow, under the string.bowed.* branch with
  // violin/viola/cello/contrabass): that path means a real, acoustic
  // bowed-string section, which a PADsynth resynthesis has no business
  // standing in for - a SoundFont's own recorded/sampled strings are the
  // right (and only convincing) source there, left untouched. GM's own
  // Synth Strings 1/2 (programs 50-51) are the deliberately synthetic
  // pair, exactly what a synthetic strings profile is suited to. "slow" (2, not 1) since that's the sustained-pad
  // half of the pair - the same reasoning "String Ensemble 2"'s own
  // "slow" naming already established for the acoustic side. "strings"
  // (real ported data, PadSynthPresets.h's own kStrings) replaces the
  // earlier invented "bowed-ensemble", dropped per explicit request.
  registerNamed(provider, "string.synth.slow", "Slow Synth Strings", makeEnsemblePad("strings", 3, 14.0f, 0.5f, 0.0f, 0.4f, 0.85f, 0.9f));

  // Synth Brass 1/2 (brass.synth/brass.synth.soft) - the same GM
  // "deliberately synthetic" distinction as lead.voice above, not the
  // fallback-only treatment organ.pipe/string.synth.slow need: brass.*
  // also has real acoustic programs (brass.trumpet/brass.section/...),
  // but brass.synth/brass.synth.soft are specifically GM's own synth-brass
  // slots (their own GmInstrumentDescriptions.h text already says "A
  // synthesized brass section"/"A softer, mellower synth brass"), so an
  // unconditional override is correct here, the same as every pad.*/
  // lead.voice entry. "saw-piano-wide"'s own power-ramp oscillator shape
  // is a bright, sawtooth-like tone - the punchy character a synth-brass
  // section wants, layered as a 3-copy ensemble for multi-voice thickness.
  // brass.synth.soft uses "saw-piano" instead - the same oscillator shape,
  // narrower/cleaner, a single voice - GM's own "softer, mellower"
  // description, closer to a sustained pad than a punchy stab.
  registerNamed(provider, "brass.synth", "Synth Brass", makeEnsemblePad("saw-piano-wide", 3, 12.0f, 0.04f, 0.0f, 0.1f, 0.85f, 0.25f));
  registerNamed(provider, "brass.synth.soft", "Soft Synth Brass", makeEnvelopePad("saw-piano", 0.25f, 0.0f, 0.3f, 0.9f, 0.6f));

  // Additive piano - <envelope>+<additive preset="piano">: three strings a
  // cent apart, every partial on the song's tuning, so septimal chords stay
  // in tune. Sustain 0 with a long decay stage (8s, past where the bank's own
  // per-partial decay has gone inaudible - see SinusoidBank's -90dB culling)
  // so the envelope never audibly truncates the tail, and a short release
  // (0.3s) so lifting the key ends the note promptly.
  auto additive_piano_envelope = []() {
    auto env = makeEnvelope(0.005f, 0.0f, 8.0f, 0.0f, 0.3f);
    auto additive = make_unique<Additive>();
    MemoryParameterSource additive_params;
    additive_params.set("preset", string("piano"));
    additive->loadParameters(additive_params);
    env->addChild(move(additive));
    return env;
  };

  // FM electric pianos after the DX7's factory voices, in place of whatever
  // the SoundFont has at these paths: its sampled pianos can't play
  // septimal intervals in tune. Each DX7 carrier and its modulators become
  // parallel two-operator layers on carriers a cent or two apart. Every
  // 1:1 layer's modulator runs a little sharp: a 1+e ratio puts partial n
  // at n + (n-1)e, so shared partials of any just interval within the
  // octave beat at most e times the fundamental, however high they are.
  //
  // Electric Piano 1 (piano.electric.tine), after E.PIANO 3: 2:1 and soft
  // 1:1 pairs for a mellow body, a 3:1 pair with feedback whose index fades
  // within a fraction of a second for the attack, and a barely audible 14:1
  // tine.
  registerNamed(provider, "piano.electric.tine", "Tine Electric Piano", makeFMLayers({
                                                                            {1.001f, 1.6f, 4.0f, -2.0f, 0.3f, 20.0f},
                                                                            {2.0f, 1.6f, 4.0f, 1.0f, 0.25f, 10.0f},
                                                                            {3.0f, 2.0f, 0.3f, 0.0f, 0.2f, 12.0f, 0.4f},
                                                                            {14.0f, 0.12f, 0.1f, 0.5f, 0.2f, 20.0f},
                                                                        }));
  // Electric Piano 2 (piano.electric.fm), after E.PIANO 2: a long 1:1 body
  // whose modulator feeds back on itself, a brighter 1:1 pair that fades
  // faster, a clangy 0.51:1 strike gone within a tenth of a second and a
  // glassy 11:1 tine.
  registerNamed(provider, "piano.electric.fm", "FM Electric Piano", makeFMLayers({
                                                                        {1.002f, 1.6f, 2.2f, -1.0f, 0.25f, 20.0f, 0.8f},
                                                                        {1.0f, 4.0f, 0.0f, 1.0f, 0.21f, 3.3f},
                                                                        {0.51f, 5.0f, 0.03f, 0.5f, 0.1f, 0.4f},
                                                                        {11.0f, 0.25f, 0.25f, 1.5f, 0.085f, 1.5f},
                                                                    }));
  // Electric Grand Piano (piano.electric.grand), after E.GRAND 2: two 1:1
  // pairs, a 3:1 pair with feedback and a 5:1 pair, all fading together so
  // the bright attack settles into a plain tone.
  registerNamed(provider, "piano.electric.grand", "Electric Grand Piano", makeFMLayers({
                                                                              {1.001f, 1.5f, 0.9f, 0.0f, 0.25f, 8.0f},
                                                                              {1.002f, 2.0f, 0.9f, 1.0f, 0.25f, 8.0f},
                                                                              {3.0f, 1.2f, 0.9f, -1.0f, 0.15f, 8.0f, 0.25f},
                                                                              {5.0f, 0.6f, 0.9f, 0.5f, 0.08f, 8.0f},
                                                                          }));
  // FX 3 (crystal), after SHIMMER: a held tone whose modulator runs a
  // little flat, a held 15.7:1 sparkle, and a decaying carrier a fifth up
  // with feedback. The voice's lowest carriers play the written note.
  registerNamed(provider, "texture.crystal", "Crystal", makeFMLayers({
                                                            {0.995f, 0.5f, 0.0f, 0.0f, 0.3f, 3.0f, 0.0f, 0.01f, 0.2f, 0.8f},
                                                            {15.71f, 0.3f, 0.0f, 0.5f, 0.25f, 3.0f, 0.0f, 0.01f, 0.2f, 0.8f},
                                                            {1.0f, 0.56f, 0.0f, 698.4f, 0.25f, 20.0f, 0.14f, 0.01f, 0.0f, 0.8f},
                                                        }));
  // Tinkle Bell, after BELLS: a plain sine at the written note and two
  // carriers 2.36 times higher, modulated at 2.67:1, for a bell's
  // inharmonic partials. The voice's lowest carrier plays the written note.
  registerNamed(provider, "percussion.pitched.metal.tinkle-bell", "Tinkle Bell", makeFMLayers({
                                                                                     {1.0f, 0.0f, 0.0f, 0.0f, 0.26f, 16.0f},
                                                                                     {2.669f, 1.33f, 1.7f, 1486.3f, 0.22f, 16.0f, 0.3f},
                                                                                     {2.669f, 0.73f, 1.7f, 1483.0f, 0.13f, 25.0f},
                                                                                 }));

  // Only takes over piano.acoustic.grand itself when nothing already
  // claimed that exact leaf - the "fallback when the SoundFont has no
  // piano" role; a real SF2 grand piano always wins when one is loaded.
  registerFallbackPath(provider, "piano.acoustic.grand", "Additive Grand Piano", additive_piano_envelope());

  // Mellotron - tapeDegradation(preset="mellotron") wrapping
  // envelope+padsynth(preset="strings") - a Mellotron "strings" tape is a
  // recording of a bowed string ensemble, the same real-world instrument
  // family as the "strings" preset, so the tape machine's own wow/
  // flutter/hiss/attack-swoop (docs/tape_degradation.md) is what actually
  // tells this apart from a live strings pad, not a different underlying
  // spectrum. Not a General MIDI instrument - GM has no mellotron program
  // - so it's registered under the existing `keyboard` taxonomy root
  // instead, following the same root.technology.name shape GM's own
  // electric-keyboard entries use (piano.electric.tine,
  // keyboard.electric.clavinet): keyboard.tape.mellotron.
  {
    auto tape = make_unique<TapeDegradation>();
    MemoryParameterSource tape_params;
    tape_params.set("preset", string("mellotron"));
    tape->loadParameters(tape_params);
    tape->addChild(makeEnvelopePad("strings", 0.05f, 0.0f, 0.3f, 0.95f, 0.4f));
    registerNamed(provider, "keyboard.tape.mellotron", "Mellotron", move(tape));
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
  case Tuning::EDO12: return 69;
  case Tuning::EDO19: return 109;
  case Tuning::EDO31: return 178;
  case Tuning::EDO53: return 304;
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
